#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")

// Estructura para almacenar los datos encontrados de la TV de forma automática
struct DispositivoDLNA {
    std::string ip;
    int puerto = 1400; 
    std::string controlUrl = "/MediaRenderer/AVTransport/Control";
};

// Ventana de selección de video nativa de Windows (Bypassea la consola)
std::wstring SeleccionarVideoVentana(HWND hWnd) {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Archivos de Video (*.mp4;*.mkv;*.avi)\0*.mp4;*.mkv;*.avi\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Elige el video para transmitir a la TV";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        return std::wstring(filename);
    }
    return L"";
}

// Escaneo en red mediante SSDP M-SEARCH (Radar WiFi)
DispositivoDLNA BuscarTelevisionEnRed() {
    DispositivoDLNA tv;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    DWORD timeout = 4000; // 4 segundos de tolerancia para buscar
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in grupoCast;
    grupoCast.sin_family = AF_INET;
    grupoCast.sin_port = htons(1900);
    grupoCast.sin_addr.s_addr = inet_addr("239.255.255.250");

    std::string mSearch = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:schemas-upnp-org:service:AVTransport:1\r\n"
        "MX: 3\r\n\r\n";

    sendto(sock, mSearch.c_str(), mSearch.length(), 0, (SOCKADDR*)&grupoCast, sizeof(grupoCast));

    char buffer[2048] = {0};
    sockaddr_in desde;
    int desdeLen = sizeof(desde);
    
    int bytesRecibidos = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (SOCKADDR*)&desde, &desdeLen);
    if (bytesRecibidos > 0) {
        std::string respuesta(buffer);
        tv.ip = inet_ntoa(desde.sin_addr);
        
        size_t locPos = respuesta.find("LOCATION: http://");
        if (locPos != std::string::npos) {
            size_t start = locPos + 17; 
            size_t end = respuesta.find("/", start);
            std::string hostPort = respuesta.substr(start, end - start);
            
            size_t colon = hostPort.find(":");
            if (colon != std::string::npos) {
                tv.puerto = std::stoi(hostPort.substr(colon + 1));
            }
        }
    }
    closesocket(sock);
    return tv;
}

// Servidor multimedia HTTP embebido (Hilo secundario en segundo plano)
void IniciarServidorMultimedia(std::string ip, int puerto, std::string rutaVideo) {
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverService;
    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = inet_addr(ip.c_str());
    serverService.sin_port = htons(puerto);
    
    if (bind(serverSocket, (SOCKADDR*)&serverService, sizeof(serverService)) == SOCKET_ERROR) return;
    listen(serverSocket, SOMAXCONN);

    while (true) {
        SOCKET acceptSocket = accept(serverSocket, NULL, NULL);
        if (acceptSocket != INVALID_SOCKET) {
            char buf[1024] = {0};
            recv(acceptSocket, buf, sizeof(buf), 0);
            
            std::ifstream file(rutaVideo, std::ios::binary | std::ios::ate);
            std::streamsize size = 0;
            if (file.is_open()) {
                size = file.tellg();
                file.seekg(0, std::ios::beg);
            }

            std::string response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: video/mp4\r\n"
                "Content-Length: " + std::to_string(size) + "\r\n"
                "Connection: close\r\n\r\n";
            send(acceptSocket, response.c_str(), response.length(), 0);

            if (file.is_open()) {
                std::vector<char> fileBuffer(4096);
                while (file.read(fileBuffer.data(), fileBuffer.size()) || file.gcount() > 0) {
                    send(acceptSocket, fileBuffer.data(), file.gcount(), 0);
                }
                file.close();
            }
            closesocket(acceptSocket);
        }
    }
}

// Inyección SOAP UPnP para interactuar con la TV
void EnviarComandoTV(std::string tvIp, int tvPuerto, std::string urlControl, std::string soapAction, std::string xmlBody) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in target;
    target.sin_family = AF_INET;
    target.sin_port = htons(tvPuerto);
    target.sin_addr.s_addr = inet_addr(tvIp.c_str());

    if (connect(sock, (SOCKADDR*)&target, sizeof(target)) == SOCKET_ERROR) {
        closesocket(sock);
        return;
    }

    std::string httpRequest = 
        "POST " + urlControl + " HTTP/1.1\r\n"
        "Host: " + tvIp + ":" + std::to_string(tvPuerto) + "\r\n"
        "Content-Length: " + std::to_string(xmlBody.length()) + "\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPACTION: \"urn:schemas-upnp-org:service:AVTransport:1#" + soapAction + "\"\r\n"
        "Connection: close\r\n\r\n" + xmlBody;

    send(sock, httpRequest.c_str(), httpRequest.length(), 0);
    closesocket(sock);
}

// Punto de entrada estándar sin ventana negra de comandos
int main(int argc, char* argv[]) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::string rutaVideo;

    // Aceptar arrastrar y soltar el archivo encima del ejecutable
    if (argc >= 2) {
        rutaVideo = argv[1];
    } else {
        // Si hacen doble clic, abre ventana de selección nativa
        std::wstring pathW = SeleccionarVideoVentana(NULL);
        if (pathW.empty()) {
            WSACleanup();
            return 0; // El usuario canceló la selección
        }
        rutaVideo = std::string(pathW.begin(), pathW.end());
    }

    // 1. Escaneo automático por WiFi
    DispositivoDLNA tv = BuscarTelevisionEnRed();
    if (tv.ip.empty()) {
        MessageBoxW(NULL, L"No se encontró ninguna TV compatible conectada a tu red WiFi actual.", L"Error de Conexión", MB_OK | MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    // 2. Resolver la IP de nuestra PC emisora
    char nombreHost[256];
    gethostname(nombreHost, sizeof(nombreHost));
    struct hostent* host = gethostbyname(nombreHost);
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    std::string miIp = inet_ntoa(addr);
    int miPuerto = 8080;
    std::string urlVideo = "http://" + miIp + ":" + std::to_string(miPuerto) + "/";

    // 3. Levantar el micro-servidor HTTP local para servir el archivo multimedia
    std::thread hiloServidor(IniciarServidorMultimedia, miIp, miPuerto, rutaVideo);
    hiloServidor.detach();
    Sleep(1200);

    // 4. Cargar el recurso multimedia en el AVTransport de la TV encontrada
    std::string xmlSetUri = 
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<s:Envelope xmlns:s=\"http://xmlsoap.org\">\n"
        "  <s:Body>\n"
        "    <u:SetAVTransportURI xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\n"
        "      <InstanceID>0</InstanceID>\n"
        "      <CurrentURI>" + urlVideo + "</CurrentURI>\n"
        "      <CurrentURIMetaData></CurrentURIMetaData>\n"
        "    </u:SetAVTransportURI>\n"
        "  </s:Body>\n"
        "</s:Envelope>";
    
    EnviarComandoTV(tv.ip, tv.puerto, tv.controlUrl, "SetAVTransportURI", xmlSetUri);
    Sleep(1000);

    // 5. Enviar señal de Play
    std::string xmlPlay = 
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<s:Envelope xmlns:s=\"http://xmlsoap.org\">\n"
        "  <s:Body>\n"
        "    <u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\n"
        "      <InstanceID>0</InstanceID>\n"
        "      <Speed>1</Speed>\n"
        "    </u:Play>\n"
        "  </s:Body>\n"
        "</s:Envelope>";

    EnviarComandoTV(tv.ip, tv.puerto, tv.controlUrl, "Play", xmlPlay);

    // 6. Cuadro de diálogo de confirmación para mantener la app viva durante la reproducción
    std::wstring msgExito = L"Transmitiendo con éxito al dispositivo en " + std::wstring(tv.ip.begin(), tv.ip.end()) + L"\n\nHaz clic en Aceptar cuando desees finalizar la transmisión del video.";
    MessageBoxW(NULL, msgExito.c_str(), L"Lite DLNA Player", MB_OK | MB_ICONINFORMATION);

    WSACleanup();
    return 0;
}
