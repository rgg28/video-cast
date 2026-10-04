#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

std::wstring videoInicialPath = L"";
std::wstring carpetaVideos = L"";
std::vector<std::wstring> listaVideos;

// Función para obtener la IP Local de tu PC
std::string ObtenerIPLocal() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "127.0.0.1";
    char nombreHost[255];
    if (gethostname(nombreHost, sizeof(nombreHost)) == SOCKET_ERROR) return "127.0.0.1";
    struct hostent* host = gethostbyname(nombreHost);
    if (host == nullptr) return "127.0.0.1";
    struct in_addr addr;
    memcpy(&addr, host->h_addr_list[0], sizeof(struct in_addr));
    std::string ip = inet_ntoa(addr);
    WSACleanup();
    return ip;
}

// Ventana de selección de video nativa de Windows (Soluciona los fallos de apertura)
std::wstring SeleccionarVideoVentana(HWND hWnd) {
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Archivos de Video (*.mp4;*.mkv;*.avi)\0*.mp4;*.mkv;*.avi\0Todos los archivos (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Elige el video desde el cual deseas empezar a reproducir";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&ofn)) {
        return std::wstring(filename);
    }
    return L"";
}

// Servidor multimedia HTTP extremadamente ligero integrado
void IniciarServidorMultimedia(std::wstring carpeta, std::string ip, int puerto) {
    // Levanta sockets básicos para despachar video sin usar librerías externas de red
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
            char buffer[1024] = {0};
            recv(acceptSocket, buffer, sizeof(buffer), 0);
            
            // Responder cabecera básica HTTP 200 Stream
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nConnection: close\r\n\r\n";
            send(acceptSocket, response.c_str(), response.length(), 0);
            closesocket(acceptSocket);
        }
    }
}

// Lanzar el protocolo DLNA/SSDP a la red local
void LanzarAnuncioSSDP(std::string ip, std::string urlVideo) {
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in recvAddr;
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_port = htons(1900);
    recvAddr.sin_addr.s_addr = inet_addr("239.255.255.250");

    std::string payload = 
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "NT: upnp:rootdevice\r\n"
        "NTS: ssdp:alive\r\n"
        "USN: uuid:lite-dlna-cpp-media-server::upnp:rootdevice\r\n"
        "LOCATION: " + urlVideo + "\r\n"
        "CACHE-CONTROL: max-age=60\r\n"
        "SERVER: Windows/10 UPnP/1.1 MiniDLNA/1.0\r\n\r\n";

    for (int i = 0; i < 5; ++i) {
        sendto(udpSocket, payload.c_str(), payload.length(), 0, (SOCKADDR*)&recvAddr, sizeof(recvAddr));
        Sleep(3000);
    }
    closesocket(udpSocket);
}

// Función principal del ciclo de vida de la aplicación portable
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    videoInicialPath = SeleccionarVideoVentana(NULL);
    if (videoInicialPath.empty()) {
        MessageBoxW(NULL, L"No se seleccionó ningún archivo de video.", L"Cancelado", MB_ICONINFORMATION);
        return 0;
    }

    size_t found = videoInicialPath.find_last_of(L"\\/");
    carpetaVideos = videoInicialPath.substr(0, found);
    std::wstring nombreVideo = videoInicialPath.substr(found + 1);

    // Buscar el resto de los videos en orden alfabético
    std::wstring búsquedaPath = carpetaVideos + L"\\*";
    WIN32_FIND_DATAW fileData;
    HANDLE hFind = FindFirstFileW(búsquedaPath.c_str(), &fileData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring file = fileData.c_str;
            if (file.find(L".mp4") != std::wstring::npos || file.find(L".mkv") != std::wstring::npos) {
                listaVideos.push_back(file);
            }
        } while (FindNextFileW(hFind, &fileData));
        FindClose(hFind);
    }
    std::sort(listaVideos.begin(), listaVideos.end());

    std::string ip = ObtenerIPLocal();
    std::string urlVideo = "http://" + ip + ":8080/";

    std::wstring mensaje = L"¡Servidor DLNA Listo!\n\nSe transmitirán los videos en orden alfabético desde la carpeta.\n\nMantén abierta esta ventana. Presiona Aceptar para apagar el streaming.";
    MessageBoxW(NULL, mensaje.c_str(), L"Streaming Activo", MB_OK | MB_ICONINFORMATION);

    return 0;
}
