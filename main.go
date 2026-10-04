package main

import (
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"://github.com"
	"://github.com/dcps/av1"
)

func main() {
	var carpeta string
	fmt.Print("📁 Introduce la ruta de la carpeta con videos (ej: C:\\Videos): ")
	fmt.Scanln(&carpeta)

	carpeta = strings.TrimSpace(carpeta)
	if _, err := os.Stat(carpeta); os.IsNotExist(err) {
		log.Fatalf("❌ La carpeta no existe: %v", err)
	}

	// 1. Obtener IP local
	ipLocal := obtenerIPLocal()
	puerto := "8080"
	urlBase := fmt.Sprintf("http://%s:%s/", ipLocal, puerto)

	// 2. Iniciar servidor HTTP ligero para los videos
	fs := http.FileServer(http.Dir(carpeta))
	http.Handle("/", fs)
	go func() {
		log.Printf("🌐 Servidor local iniciado en %s", urlBase)
		if err := http.ListenAndServe(":"+puerto, nil); err != nil {
			log.Fatalf("Error en el servidor: %v", err)
		}
	}()

	// 3. Listar videos disponibles
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
		log.Fatal("❌ No se encontraron videos (.mp4, .mkv, .avi) en la carpeta.")
	}

	var opcion int
	fmt.Print("\n🔢 Selecciona el número del video a transmitir: ")
	fmt.Scanln(&opcion)

	if opcion < 0 || opcion >= len(videos) {
		log.Fatal("❌ Selección inválida.")
	}
	videoSeleccionado := videos[opcion]

	// 4. Buscar Smart TV / Dispositivo DLNA (UPnP)
	fmt.Println("\n🔍 Buscando Smart TVs en la red (DLNA/UPnP)...")
	dispositivos, err := goupnp.DiscoverDevices("urn:schemas-upnp-org:device:MediaRenderer:1")
	if err != nil || len(dispositivos) == 0 {
		log.Fatal("❌ No se encontraron televisores DLNA en la red. Asegúrate de estar en el mismo WiFi.")
	}

	// Conectarse al primer televisor encontrado
	dev := dispositivos[0]
	fmt.Printf("📺 ¡Televisor encontrado!: %s\n", dev.Root.Device.FriendlyName)

	avClient := av1.NewAVTransport1ClientsFromRootDevice(dev.Root, dev.Location)
	if len(avClient) == 0 {
		log.Fatal("❌ El dispositivo encontrado no soporta control de reproducción AVTransport.")
	}
	client := avClient[0]

	// 5. Enviar URL del video a la TV
	urlVideo := urlBase + videoSeleccionado
	fmt.Printf("🚀 Transmitiendo: %s a la TV...\n", videoSeleccionado)

	err = client.SetAVTransportURI(0, urlVideo, "")
	if err != nil {
		log.Fatalf("❌ Error al enviar el video a la TV: %v", err)
	}

	err = client.Play(0, "1")
	if err != nil {
		log.Fatalf("❌ Error al iniciar la reproducción: %v", err)
	}

	fmt.Println("▶️ ¡Reproduciendo! Presiona ENTER para salir y detener el streaming.")
	fmt.Scanln()
	client.Stop(0)
}

func obtenerIPLocal() string {
	interfaces, err := net.Interfaces()
	if err != nil {
		return "127.0.0.1"
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, addr := range addrs {
			var ip net.IP
			switch v := addr.(type) {
			case *net.IPNet:
				ip = v.IP
			case *net.IPAddr:
				ip = v.IP
			}
			if ip == nil || ip.IsLoopback() {
				continue
			}
			ip = ip.To4()
			if ip != nil {
				return ip.String()
			}
		}
	}
	return "127.0.0.1"
}
