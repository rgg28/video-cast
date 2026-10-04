package main

import (
	"bufio"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func main() {
	reader := bufio.NewReader(os.Stdin)

	fmt.Print("📁 Introduce la ruta de la carpeta con videos (ej: C:\\Videos): ")
	carpeta, _ := reader.ReadString('\n')
	carpeta = strings.TrimSpace(carpeta)

	if _, err := os.Stat(carpeta); os.IsNotExist(err) {
		fmt.Printf("❌ La carpeta no existe: %s\n", carpeta)
		pausarYSalir()
		return
	}

	ipLocal := obtenerIPLocal()
	puerto := "8080"
	urlBase := fmt.Sprintf("http://%s:%s/", ipLocal, puerto)

	// Servidor de archivos nativo
	fs := http.FileServer(http.Dir(carpeta))
	http.Handle("/", fs)
	go func() {
		_ = http.ListenAndServe(":"+puerto, nil)
	}()

	archivos, _ := os.ReadDir(carpeta)
	var videos []string
	fmt.Println("\n🎬 Videos encontrados:")
	for _, archivo := range archivos {
		ext := strings.ToLower(filepath.Ext(archivo.Name()))
		if ext == ".mp4" || ext == ".mkv" || ext == ".avi" {
			videos = append(videos, archivo.Name())
			fmt.Printf("[%d] %s\n", len(videos)-1, archivo.Name())
		}
	}

	if len(videos) == 0 {
		fmt.Println("❌ No se encontraron videos compatibles (.mp4, .mkv, .avi) en la carpeta.")
		pausarYSalir()
		return
	}

	fmt.Print("\n🔢 Selecciona el número del video a transmitir: ")
	opcionStr, _ := reader.ReadString('\n')
	opcionStr = strings.TrimSpace(opcionStr)
	opcion, err := strconv.Atoi(opcionStr)

	if err != nil || opcion < 0 || opcion >= len(videos) {
		fmt.Println("❌ Selección inválida.")
		pausarYSalir()
		return
	}
	videoSeleccionado := videos[opcion]
	urlVideo := urlBase + videoSeleccionado

	// Anuncio UPnP/DLNA nativo mediante SSDP (Simple Service Discovery Protocol)
	fmt.Println("\n🚀 Transmitiendo servicio DLNA en la red local...")
	fmt.Printf("🔗 Enlace del stream: %s\n", urlVideo)
	fmt.Println("📺 Abre la sección 'Dispositivos de Red' o 'Reproductor Multimedia' en tu TV.")
	
	go lanzarAnuncioSSDP(ipLocal, puerto)

	fmt.Println("\n▶️ Servidor activo. Presiona ENTER para cerrar el streaming.")
	_, _ = reader.ReadString('\n')
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
			"LOCATION: http://%s:%s/description.xml\r\n"+
			"CACHE-CONTROL: max-age=1800\r\n"+
			"SERVER: Windows/10 UPnP/1.1 MiniDLNA/1.0\r\n\r\n", ip, puerto)

	for {
		_, _ = conn.Write([]byte(payload))
		time.Sleep(5 * time.Second)
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

func pausarYSalir() {
	fmt.Println("\nPresiona ENTER para salir.")
	var b [1]byte
	_, _ = os.Stdin.Read(b[:])
}
