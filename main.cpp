#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <upnp.h> // API nativa de Windows para control de dispositivos UPnP/DLNA
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

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

// Buscar usando el sistema nativo de Windows (Bypassea bloqueos del router)
void DescubrirDispositivosConWindows() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IUPnPDeviceFinder* pDeviceFinder = NULL;
    
    hr = CoCreateInstance(CLSID_UPnPDeviceFinder, NULL, CLSCTX_INPROC_SERVER, IID_IUPnPDeviceFinder, (void**)&pDeviceFinder);
    if (SUCCEEDED(hr)) {
        BSTR bstrType = SysAllocString(L"urn:schemas-upnp-org:device:MediaRenderer:1");
        IUPnPDevices* pDevices = NULL;
        
        // Windows busca directamente en su propia caché de red
        hr = pDeviceFinder->FindByType(bstrType, 0, &pDevices);
        if (SUCCEEDED(hr) && pDevices != NULL) {
            long count = 0;
            pDevices->get_Count(&count);
            
            IUnknown* pUnk = NULL;
            pDevices->get__NewEnum(&pUnk);
            if (pUnk) {
                IEnumVARIANT* pEnum = NULL;
                pUnk->QueryInterface(IID_IEnumVARIANT, (void**)&pEnum);
                if (pEnum) {
                    VARIANT var;
                    VariantInit(&var);
                    while (pEnum->Next(1, &var, NULL) == S_OK) {
                        IUPnPDevice* pDevice = NULL;
                        var.punkVal->QueryInterface(IID_IUPnPDevice, (void**)&pDevice);
                        if (pDevice) {
                            BSTR bstrName = NULL;
                            BSTR bstrLoc = NULL;
                            pDevice->get_FriendlyName(&bstrName);
                            pDevice->get_PresentationURL(&bstrLoc); // URL que contiene la IP
                            
                            if (bstrName) {
                                DispositivoTV tv;
                                tv.nombre = bstrName;
                                tv.puerto = 7676; // Puerto base
                                tv.urlControl = "/MediaRenderer/AVTransport/Control";
                                
                                if (bstrLoc) {
                                    std::wstring locStr(bstrLoc);
                                    size_t start = locStr.find(L"//");
                                    if (start != std::wstring::npos) {
                                        size_t end = locStr.find(L"/", start + 2);
                                        std::wstring host = locStr.substr(start + 2, end - (start + 2));
                                        size_t colon = host.find(L":");
                                        std::wstring ipW = (colon != std::wstring::npos) ? host.substr(0, colon) : host;
                                        tv.ip = std::string(ipW.begin(), ipW.end());
                                        if (colon != std::wstring::npos) {
                                            tv.puerto = std::stoi(host.substr(colon + 1));
                                        }
                                    }
                                    SysFreeString(bstrLoc);
                                }
                                
                                // Si Windows no provee la URL de presentación, intentamos extraer datos básicos
                                if (tv.ip.empty()) {
                                    tv.ip = "192.168.1.50"; // Fallback por defecto si está oculta
                                }
                                
                                listaTelevisiones.push_back(tv);
                                SysFreeString(bstrName);
                            }
                            pDevice->Release();
                        }
                        VariantClear(&var);
                    }
                    pEnum->Release();
                }
                pUnk->Release();
            }
            pDevices->Release();
        }
        SysFreeString(bstrType);
        pDeviceFinder->Release();
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
            
            if (listaTelevisiones.empty()) {
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"No se encontraron pantallas en la red de Windows.");
            } else {
                for (const auto& tv : listaTelevisiones) {
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)tv.nombre.c_str());
                }
            }
            CreateWindowExW(0, L"BUTTON", L"Transmitir", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 140, 210, 100, 30, hwnd, (HMENU)IDOK, NULL, NULL);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                int index = SendMessageW(hList, LB_GETCURSEL, 0, 0);
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

// Servidor multimedia HTTP común
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
            char buf[1024] = {0};
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
    if (argc >= 2) { rutaVideo = argv[1]; } 
    else {
        std::wstring pathW = SeleccionarVideoVentana(NULL);
        if (pathW.empty()) { WSACleanup(); return 0; }
        rutaVideo = std::string(pathW.begin(), pathW.end());
    }

    // 1. Forzar a Windows a darnos la lista real de pantallas que ve en la red
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
// 3. Lanzar la ventana nativa de selección
int seleccion = DialogBoxIndirectParamW(NULL, (LPDLGTEMPLATEW)dlgData.data(), NULL, VentanaSeleccionProc, 0);
if (seleccion < 0 || seleccion >= (int)listaTelevisiones.size()) {
WSACleanup();
return 0;
}
// Dispositivo elegido con el clic
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
// Probar variaciones de endpoints comunes sobre la IP seleccionada automáticamente
std::string endpoints[] = { tvSeleccionada.urlControl, "/MediaRenderer/AVTransport/Control", "/AVTransport/Control" };
int puertos[] = { tvSeleccionada.puerto, 7676, 1400, 49153 };
for (int p : puertos) {
for (const auto& endp : endpoints) {
EnviarComandoTV(tvSeleccionada.ip, p, endp, "SetAVTransportURI", xmlSetUri);
Sleep(150);
EnviarComandoTV(tvSeleccionada.ip, p, endp, "Play", xmlPlay);
}
}
std::wstring msgFin = L"Transmitiendo en segundo plano a: " + tvSeleccionada.nombre + L"\n\nPresiona Aceptar para desconectar.";
MessageBoxW(NULL, msgFin.c_str(), L"Lite DLNA Player", MB_OK | MB_ICONINFORMATION);
WSACleanup();
return 0;
}
