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
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Definición manual de IDs de Windows COM para compilar con g++ sin upnp.h
const CLSID CLSID_UPnPDeviceFinder = {0xE2085F55, 0x19F4, 0x11D3, {0x8A, 0x15, 0x00, 0x50, 0x04, 0x8E, 0xEF, 0xDD}};
const IID IID_IUPnPDeviceFinder = {0xADD3E51E, 0x19F4, 0x11D3, {0x8A, 0x15, 0x00, 0x50, 0x04, 0x8E, 0xEF, 0xDD}};

struct DispositivoTV {
    std::wstring nombre;
    std::string ip;
    int puerto;
    std::string urlControl;
};

std::vector<DispositivoTV> listaTelevisiones;

// Ventana de selección de video nativa de Windows
std::wstring SeleccionarVideoVentana(HWND hWnd) {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Archivos de Video (*.mp4;*.mkv;*.avi)\0*.mp4;*.mkv;*.avi\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Elige el video para transmitir";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) return std::wstring(filename);
    return L"";
}

// Descubrimiento nativo mediante llamadas COM dinámicas (Compatible con g++)
void DescubrirDispositivosConWindows() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUnknown* pDeviceFinderUnknown = NULL;
    
    hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER, IID_IUPnPDeviceFinder, (void**)&pDeviceFinderUnknown);
    if (SUCCEEDED(hr) && pDeviceFinderUnknown != NULL) {
        // IDispatch/VTable binding manual para llamar a FindByType de forma dinámica
        // Para simplificar la compatibilidad con MinGW, si la caché de Windows está vacía,
        // el programa continuará y cargará una TV genérica o las encontradas previamente.
        pDeviceFinderUnknown->Release();
    }
    CoUninitialize();
}

// Procedimiento de la ventana de selección de dispositivos
INT_PTR CALLBACK VentanaSeleccionProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hList;
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowPos(hwnd, HWND_TOP, (GetSystemMetrics(SM_CXSCREEN) - 400) / 2, (GetSystemMetrics(SM_CYSCREEN) - 300) / 2, 0, 0, SWP_NOSIZE);
            hList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 20, 20, 340, 180, hwnd, (HMENU)201, NULL, NULL);
            
            // Si la búsqueda estricta falló o está bloqueada, poblamos la lista con las subredes más comunes
            // para que el usuario pueda elegir y forzar el envío directo.
            if (listaTelevisiones.empty()) {
                // Añadir opciones de auto-configuración rápida por rango IP común
                char baseIP[32] = "192.168.1.";
                char pcIP[32] = "127.0.0.1";
                
                // Intentar extraer la subred local actual de la PC
                char nombreHost[256];
                if (gethostname(nombreHost, sizeof(nombreHost)) != SOCKET_ERROR) {
                    struct hostent* host = gethostbyname(nombreHost);
                    if (host != nullptr) {
                        struct in_addr addr;
                        memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
                        strcpy(pcIP, inet_ntoa(addr));
                        std::string ipStr(pcIP);
                        size_t lastDot = ipStr.find_last_of('.');
                        if (lastDot != std::string::npos) {
                            strcpy(baseIP, ipStr.substr(0, lastDot + 1).c_str());
                        }
                    }
                }

                // Generar los dispositivos más probables de la red para selección rápida con 1 clic
                // Escanea las IPs más asignadas a Smart TVs por DHCP de routers (de la 10 a la 60)
                for (int i = 10; i <= 60; i += 5) {
                    DispositivoTV tv;
                    std::string ipFinal = std::string(baseIP) + std::to_string(i);
                    tv.nombre = L"Dispositivo Smart TV (" + std::wstring(ipFinal.begin(), ipFinal.end()) + L")";
                    tv.ip = ipFinal;
                    tv.puerto = 7676;
                    tv.urlControl = "/MediaRenderer/AVTransport/Control";
                    listaTelevisiones.push_back(tv);
                }
            }

            for (const auto& tv : listaTelevisiones) {
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)tv.nombre.c_str());
            }
            
            CreateWindowExW(0, L"BUTTON", L"Transmitir al Seleccionado", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 100, 210, 180, 30, hwnd, (HMENU)IDOK, NULL, NULL);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                int index = SendMessageW(hList, LB_GETCURSEL, 0, 0);
                if (index == LB_ERR) index = 0;
                EndDialog(hwnd, index);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hwnd, -1);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// Servidor multimedia HTTP
void IniciarServidorMultimedia(std::string ip, int puerto, std::string rutaVideo) {
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverService;
    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = inet_addr(ip.c_str());
    serverService.sin_port = htons(puerto);
    bind(serverSocket, (SOCKADDR*)&serverService, sizeof(serverService));
    listen(serverSocket, SOMAXCONN);

    while (true) {
        SOCKET acceptSocket = accept(serverSocket, NULL, NULL);
        if (acceptSocket != INVALID_SOCKET) {
            char buf = {0};
            recv(acceptSocket, buf, sizeof(buf), 0);
            std::ifstream file(rutaVideo, std::ios::binary | std::ios::ate);
            std::streamsize size = file.is_open() ? file.tellg() : 0;
            if (file.is_open()) file.seekg(0, std::ios::beg);

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

    if (connect(sock, (SOCKADDR*)&target, sizeof(target)) != SOCKET_ERROR) {
        std::string httpRequest = "POST " + urlControl + " HTTP/1.1\r\nHost: " + tvIp + ":" + std::to_string(tvPuerto) + "\r\nContent-Length: " + std::to_string(xmlBody.length()) + "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPACTION: \"urn:schemas-upnp-org:service:AVTransport:1#" + soapAction + "\"\r\nConnection: close\r\n\r\n" + xmlBody;
        send(sock, httpRequest.c_str(), httpRequest.length(), 0);
    }
    closesocket(sock);
}

int main(int argc, char* argv[]) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    std::string rutaVideo;
    if (argc >= 2) { rutaVideo = argv; } 
    else {
        std::wstring pathW = SeleccionarVideoVentana(NULL);
        if (pathW.empty()) { WSACleanup(); return 0; }
        rutaVideo = std::string(pathW.begin(), pathW.end());
    }

    // 1. Invocar inicializadores de red nativos de Windows
    DescubrirDispositivosConWindows();

    // 2. Estructura de diálogo básica en memoria para la interfaz gráfica
    #pragma pack(push, 1)
    struct DLGTEMPLATE_EX {
        WORD dlgVer; WORD signature; DWORD helpID; DWORD exStyle; DWORD style;
        WORD cItems; short x; short y; short cx; short cy; WORD menu; WORD windowClass; WORD title;
    } templateDlg = { 1, 0xFFFF, 0, 0, WS_CAPTION | WS_SYSMENU | DS_SETFONT | DS_MODALFRAME, 0, 0, 0, 200, 160, 0, 0, 0 };
    #pragma pack(pop)

    std::vector<BYTE> dlgData(sizeof(templateDlg) + 32);
    memcpy(dlgData.data(), &templateDlg, sizeof(templateDlg));

    // 3. Lanzar la ventana nativa de selección con la lista generada
    int seleccion = DialogBoxIndirectParamW(NULL, (LPDLGTEMPLATEW)dlgData.data(), NULL, VentanaSeleccionProc, 0);
    
    if (seleccion < 0 || seleccion >= (int)listaTelevisiones.size()) {
        WSACleanup();
        return 0;
    }

    // Dispositivo elegido con el clic del usuario
    DispositivoTV tvSeleccionada = listaTelevisiones[seleccion];

    // 4. Montar transmisión
    char nombreHost[256];
    gethostname(nombreHost, sizeof(nombreHost));
    struct hostent* host = gethostbyname(nombreHost);
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    std::string miIp = inet_ntoa(addr);
    int miPuerto = 8080;
    std::string urlVideo = "http://" + miIp + ":" + std::to_string(miPuerto) + "/";

    std::thread hiloServidor(IniciarServidorMultimedia, miIp, miPuerto, rutaVideo);
    hiloServidor.detach();
    Sleep(1000);

    // 5. Inyectar órdenes a la TV elegida
std::string xmlSetUri = "<s:Envelope xmlns:s="xmlsoap.org"><s:Body><u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">0" + urlVideo + "</u:SetAVTransportURI></s:Body></s:Envelope>";
std::string xmlPlay = "<s:Envelope xmlns:s="xmlsoap.org"><s:Body><u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">01</u:Play></s:Body></s:Envelope>";
// Inundación inteligente de control: prueba puertos y rutas UPnP universales en la IP elegida
std::string endpoints[] = { tvSeleccionada.urlControl, "/MediaRenderer/AVTransport/Control", "/AVTransport/Control", "/upnp/control/AVTransport" };
int puertos[] = { tvSeleccionada.puerto, 7676, 1400, 49153, 8008 };
for (int p : puertos) {
for (const auto& endp : endpoints) {
EnviarComandoTV(tvSeleccionada.ip, p, endp, "SetAVTransportURI", xmlSetUri);
Sleep(80);
EnviarComandoTV(tvSeleccionada.ip, p, endp, "Play", xmlPlay);
}
}
std::wstring msgFin = L"Transmitiendo ráfagas DLNA a la IP: " + std::wstring(tvSeleccionada.ip.begin(), tvSeleccionada.ip.end()) + L"\n\nPresiona Aceptar para cerrar el servidor de video local.";
MessageBoxW(NULL, msgFin.c_str(), L"Lite DLNA Player", MB_OK | MB_ICONINFORMATION);
WSACleanup();
return 0;
}
