package main

import (
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

var (
	user32           = syscall.NewLazyDLL("user32.dll")
	shell32          = syscall.NewLazyDLL("shell32.dll")
	procMessageBoxW  = user32.NewProc("MessageBoxW")
	procSHBrowseFor  = shell32.NewProc("SHBrowseForFolderW")
	procSHGetPath    = shell32.NewProc("SHGetPathFromIDListW")
	procCoInitialize = syscall.NewLazyDLL("ole32.dll").NewProc("CoInitialize")
)

type BROWSEINFO struct {
	HwndOwner      uintptr
	PidlRoot       uintptr
	PszDisplayName uintptr
	LpszTitle      uintptr
	UlFlags        uint32
	Lpfn           uintptr
	LParam         uintptr
	IImage         int32
}

func main() {
	_, _, _ = procCoInitialize.Call(0)

	carpeta := seleccionarCarpetaVentana("Selecciona la carpeta que contiene tus videos:")
	if carpeta == "" {
		mostrarMensaje("Cancelado", "No se seleccionó ninguna carpeta. El programa se cerrará.")
		return
	}

	archivos, err := os.ReadDir(carpeta)
	if err != nil {
		mostrarMensaje("Error", "No se pudo leer la carpeta seleccionada.")
		return
	}

	var videos []string
	for _, archivo := range archivos {
		ext := strings.ToLower(filepath.Ext(archivo.Name()))
		if ext == ".mp4" || ext == ".mkv" || ext == ".avi" {
			videos = append(videos, archivo.Name())
		}
	}

	if len(videos) == 0 {
		mostrarMensaje("Aviso", "No se encontraron videos (.mp4, .mkv, .avi) en la carpeta seleccionada.")
		return
	}

	videoSeleccionado := videos[0]
	ipLocal := obtenerIPLocal()
	puerto := "8080"
	urlVideo := fmt.Sprintf("http://%s:%s/%s", ipLocal, puerto, videoSeleccionado)

	pregunta := fmt.Sprintf("Se detectaron %d videos.\n\n¿Deseas iniciar la transmisión de:\n%s?\n\nAl confirmar, búscalo en tu Smart TV.", len(videos), videoSeleccionado)
	if !mostrarConfirmacion("Mini Transmisor DLNA", pregunta) {
		return
	}

	fs := http.FileServer(http.Dir(carpeta))
	http.Handle("/", fs)
	go func() {
		_ = http.ListenAndServe(":"+puerto, nil)
	}()

	go lanzarAnuncioSSDP(ipLocal, puerto)

	mostrarMensaje("Streaming Activo", fmt.Sprintf("🚀 Transmitiendo con éxito.\n\nURL: %s\n\nMantén esta ventana abierta. Presiona OK cuando desees finalizar.", urlVideo))
}

func seleccionarCarpetaVentana(titulo string) string {
	titlePtr, _ := syscall.UTF16PtrFromString(titulo)
	var bi BROWSEINFO
	bi.LpszTitle = uintptr(unsafe.Pointer(titlePtr))
	bi.UlFlags = 0x00000001

	pidl, _, _ := procSHBrowseFor.Call(uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return ""
	}

	var path [260]uint16
	ret, _, _ := procSHGetPath.Call(pidl, uintptr(unsafe.Pointer(&path)))
	if ret == 0 {
		return ""
	}

	return syscall.UTF16ToString(path[:])
}

func mostrarMensaje(titulo, contenido string) {
	tPtr, _ := syscall.UTF16PtrFromString(titulo)
	cPtr, _ := syscall.UTF16PtrFromString(contenido)
	_, _, _ = procMessageBoxW.Call(0, uintptr(unsafe.Pointer(cPtr)), uintptr(unsafe.Pointer(tPtr)), 0x00000000)
}

func mostrarConfirmacion(titulo, contenido string) bool {
	tPtr, _ := syscall.UTF16PtrFromString(titulo)
	cPtr, _ := syscall.UTF16PtrFromString(contenido)
	ret, _, _ := procMessageBoxW.Call(0, uintptr(unsafe.Pointer(cPtr)), uintptr(unsafe.Pointer(tPtr)), 0x00000001)
	return ret == 1
}

func lanzarAnuncioSSDP(ip, puerto string) {
	addr, _ := net.ResolveUDPAddr("udp", "239.255.255.250:1900")
	conn, _ := net.DialUDP("udp", nil, addr)
	defer conn.Close()

	payload := fmt.Sprintf(
		"NOTIFY * HTTP/1.1\r\n"+
			"HOST: 239.255.255.250:1900\r\n"+
			"NT: upnp:rootdevice\r\n"+
			"NTS: ssdp:alive\r\n"+
			"USN: uuid:lite-dlna-media-server::upnp:rootdevice\r\n"+
			"LOCATION: http://%s:%s/\r\n"+
			"CACHE-CONTROL: max-age=1800\r\n"+
			"SERVER: Windows/10 UPnP/1.1 MiniDLNA/1.0\r\n\r\n", ip, puerto)

	for {
		_, _ = conn.Write([]byte(payload))
		time.Sleep(4 * time.Second)
	}
}

func obtenerIPLocal() string {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return "127.0.0.1"
	}
	for _, address := range addrs {
		if ipnet, ok := address.(*net.IPNet); ok && !ipnet.IP.IsLoopback() {
			if ipnet.IP.To4() != nil {
				return ipnet.IP.String()
			}
		}
	}
	return "127.0.0.1"
}
