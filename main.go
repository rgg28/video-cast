package main

import (
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"time"
	"unsafe"
)

var (
	user32              = syscall.NewLazyDLL("user32.dll")
	comdlg32            = syscall.NewLazyDLL("comdlg32.dll")
	procMessageBoxW     = user32.NewProc("MessageBoxW")
	procGetOpenFileName = comdlg32.NewProc("GetOpenFileNameW")
	procCoInitialize    = syscall.NewLazyDLL("ole32.dll").NewProc("CoInitialize")
)

type OPENFILENAMEW struct {
	StructSize       uint32
	HwndOwner        uintptr
	HInstance        uintptr
	Filter           *uint16
	CustomFilter     *uint16
	MaxCustomFilter  uint32
	FilterIndex      uint32
	File             *uint16
	MaxFile          uint32
	FileTitle        *uint16
	MaxFileTitle     uint32
	InitialDir       *uint16
	Title            *uint16
	Flags            uint32
	FileOffset       uint16
	FileExtension    uint16
	DefExt           *uint16
	CustData         uintptr
	LpfHook          uintptr
	LpTemplateName   *uint16
	PvReserved       uintptr
	DwReserved       uint32
	FlagsEx          uint32
}

func main() {
	_, _, _ = procCoInitialize.Call(0)

	// 1. Seleccionar el video por el que deseas empezar
	rutaPrimerVideo := seleccionarVideoVentana()
	if rutaPrimerVideo == "" {
		mostrarMensaje("Cancelado", "No se seleccionó ningún video inicial.")
		return
	}

	carpeta := filepath.Dir(rutaPrimerVideo)
	videoInicialNombre := filepath.Base(rutaPrimerVideo)

	// 2. Leer todos los videos de la carpeta y ordenarlos alfabéticamente
	archivos, err := os.ReadDir(carpeta)
	if err != nil {
		mostrarMensaje("Error", "No se pudo acceder a la carpeta de los videos.")
		return
	}

	var todosLosVideos []string
	for _, archivo := range archivos {
		ext := strings.ToLower(filepath.Ext(archivo.Name()))
		if ext == ".mp4" || ext == ".mkv" || ext == ".avi" {
			todosLosVideos = append(todosLosVideos, archivo.Name())
		}
	}
	sort.Strings(todosLosVideos) // Orden alfabético estricto

	// 3. Filtrar la lista para empezar desde el elegido en adelante
	indiceInicial := -1
	for i, v := range todosLosVideos {
		if v == videoInicialNombre {
			indiceInicial = i
			break
		}
	}

	if indiceInicial == -1 {
		mostrarMensaje("Error", "No se pudo verificar la posición del video seleccionado.")
		return
	}

	listaReproduccion := todosLosVideos[indiceInicial:]

	// 4. Configurar Servidor de Red Local
	ipLocal := obtenerIPLocal()
	puerto := "8080"
	urlBase := fmt.Sprintf("http://%s:%s/", ipLocal, puerto)

	fs := http.FileServer(http.Dir(carpeta))
	http.Handle("/", fs)
	go func() {
		_ = http.ListenAndServe(":"+puerto, nil)
	}()

	// 5. Confirmación e Inicio de Transmisión DLNA continua
	pregunta := fmt.Sprintf("¡Todo listo!\n\nSe reproducirán %d videos en orden alfabético comenzando por:\n➡ %s\n\n¿Deseas iniciar la transmisión?", len(listaReproduccion), videoInicialNombre)
	if !mostrarConfirmacion("Mini Transmisor DLNA", pregunta) {
		return
	}

	// Transmisión asíncrona de la tanda de videos mediante anuncios SSDP secuenciales
	go func() {
		for _, video := range listaReproduccion {
			videoEscapado := strings.ReplaceAll(video, " ", "%20")
			urlCompleta := urlBase + videoEscapado
			
			// Anunciamos este video específico de forma constante durante 15 segundos para que la TV lo capture y cargue
			finAnuncio := time.Now().Add(15 * time.Second)
			go lanzarAnuncioSSDP(ipLocal, puerto, urlCompleta, finAnuncio)
			
			// Esperamos un tiempo prudente antes de preparar el siguiente de la lista secuencial
			time.Sleep(20 * time.Second) 
		}
	}()

	mostrarMensaje("Streaming Activo", fmt.Sprintf("🚀 Transmitiendo cola ordenada de videos en tu red local.\n\nAbra el 'Reproductor Multimedia' de su Smart TV.\n\nPresione Aceptar cuando desee cerrar el servidor multimedia."))
}

func seleccionarVideoVentana() string {
	var ofn OPENFILENAMEW
	ofn.StructSize = uint32(unsafe.Sizeof(ofn))
	filtroTexto, _ := syscall.UTF16FromString("Archivos de Video (*.mp4;*.mkv;*.avi)\x00*.mp4;*.mkv;*.avi\x00")
	ofn.Filter = &filtroTexto
	bufferArchivo := make([]uint16, 1024)
	ofn.File = &bufferArchivo
	ofn.MaxFile = uint32(len(bufferArchivo))
	tituloTexto, _ := syscall.UTF16FromString("Elige el video desde el cual deseas empezar a reproducir")
	ofn.Title = &tituloTexto
	ofn.Flags = 0x00001000 | 0x00000004
	ret, _, _ := procGetOpenFileName.Call(uintptr(unsafe.Pointer(&ofn)))
	if ret == 0 {
		return ""
	}
	return syscall.UTF16ToString(bufferArchivo)
}

func lanzarAnuncioSSDP(ip, puerto, urlVideo string, fin time.Time) {
	addr, _ := net.ResolveUDPAddr("udp", "239.255.255.250:1900")
	conn, _ := net.DialUDP("udp", nil, addr)
	defer conn.Close()

	payload := fmt.Sprintf(
		"NOTIFY * HTTP/1.1\r\n"+
			"HOST: 239.255.255.250:1900\r\n"+
			"NT: upnp:rootdevice\r\n"+
			"NTS: ssdp:alive\r\n"+
			"USN: uuid:lite-dlna-media-server::upnp:rootdevice\r\n"+
			"LOCATION: %s\r\n"+
			"CACHE-CONTROL: max-age=60\r\n"+
			"SERVER: Windows/10 UPnP/1.1 MiniDLNA/1.0\r\n\r\n", urlVideo)

	for time.Now().Before(fin) {
		_, _ = conn.Write([]byte(payload))
		time.Sleep(3 * time.Second)
	}
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
