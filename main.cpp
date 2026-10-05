#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

// Estructura para almacenar los datos encontrados de la TV
struct DispositivoDLNA {
    std::string ip;
    int puerto = 1400; // Puerto por defecto si no se encuentra
    std::string controlUrl = "/MediaRenderer/AVTransport/Control";
};

// Función para buscar la TV en la red WiFi usando SSDP M-SEARCH
DispositivoDLNA BuscarTelevisionEnRed() {
    DispositivoDLNA tv;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    // Configurar tiempo de espera (Timeout) para que no se quede colgado si no hay TV
    DWORD timeout = 4000; // 4 segundos
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    sockaddr_in grupoCast;
    grupoCast.sin_family = AF_INET;
    grupoCast.sin_port = htons(1900);
    grupoCast.sin_addr.s_addr = inet_addr("239.255.255.250");

    // Comando de búsqueda estándar UPnP para renderizadores de video (TVs)
    std::string mSearch = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "ST: urn:schemas-upnp-org:service:AVTransport:1\r\n"
        "MX: 3\r\n\r\n";

    std::cout << "Escaneando la red WiFi en busca de una TV compatible...\n";
    sendto(sock, mSearch.c_str(), mSearch.length(), 0, (SOCKADDR*)&grupoCast, sizeof(grupoCast));

    char buffer[2048] = {0};
    sockaddr_in desde;
    int desdeLen = sizeof(desde);
    
    // Recibir la respuesta de la TV
    int bytesRecibidos = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (SOCKADDR*)&desde, &desdeLen);
    if (bytesRecibidos > 0) {
        std::string respuesta(buffer);
        std::cout << "¡Dispositivo encontrado!\n";
        
        // Extraer la IP del remitente
        tv.ip = inet_ntoa(desde.sin_addr);
        
        // Intentar buscar la línea LOCATION para extraer el puerto exacto
        size_t locPos = respuesta.find("LOCATION: http://");
        if (locPos != std::string::npos) {
            size_t start = locPos + 17; // Saltarse "LOCATION: http://"
            size_t end = respuesta.find("/", start);
            std::string hostPort = respuesta.substr(start, end - start);
            
            size_t colon = hostPort.find(":");
            if (colon != std::string::npos) {
                tv.puerto = std::stoi(hostPort.substr(colon + 1));
            }
        }
        std::cout << "TV Detectada en -> IP: " << tv.ip << " | Puerto: " << tv.puerto << "\n";
    } else {
        std::cout << "No se recibio respuesta de ninguna TV en el tiempo limite.\n";
    }

    closesocket(sock);
    return tv;
}

// Servidor multimedia HTTP (En hilo secundario)
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

            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: " + std::to_string(size) + "\r\nConnection: close\r\n\r\n";
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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Uso: HerramientaDLNA.exe <RUTA_VIDEO_MP4>\n";
        return 1;
    }
    std::string rutaVideo = argv[1];

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    // 1. Escaneo automático por WiFi
    DispositivoDLNA tv = BuscarTelevisionEnRed();
    if (tv.ip.empty()) {
        std::cout << "Error: Asegurate de estar en la misma red WiFi que la TV.\n";
        WSACleanup();
        return 1;
    }

    // 2. Obtener tu propia IP local
    char nombreHost[256];
    gethostname(nombreHost, sizeof(nombreHost));
    struct hostent* host = gethostbyname(nombreHost);
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    std::string miIp = inet_ntoa(addr);
    int miPuerto = 8080;
    std::string urlVideo = "http://" + miIp + ":" + std::to_string(miPuerto) + "/";

    // 3. Levantar servidor de video
    std::thread hiloServidor(IniciarServidorMultimedia, miIp, miPuerto, rutaVideo);
    hiloServidor.detach();
    Sleep(1000);

    // 4. Cargar Video en la TV (SetAVTransportURI)
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
    
    std::cout << "Cargando video en la TV detectada...\n";
    EnviarComandoTV(tv.ip, tv.puerto, tv.controlUrl, "SetAVTransportURI", xmlSetUri);
    Sleep(1000);

    // 5. Darle Play
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

    std::cout << "¡Reproduciendo!\n";
    EnviarComandoTV(tv.ip, tv.puerto, tv.controlUrl, "Play", xmlPlay);

    // Tiempo de transmisión (ajusta según tus necesidades en GitHub Actions)
    Sleep(45000); 

    WSACleanup();
    return 0;
}
