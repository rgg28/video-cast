package main

import (
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"://github.com"
	. "://github.com/declarative"
)

var (
	carpetaSeleccionada string
	ipLocal             string
	puerto              = "8080"
	servidorIniciado    = false
)

func main() {
	ipLocal = obtenerIPLocal()

	var mainWindow *walk.MainWindow
	var lbVideos *walk.ListBox
	var btnTransmitir *walk.PushButton
	var lblCarpeta *walk.Label
	videoModel := walk.NewSimpleListModel()

	err := MainWindow{
		AssignTo: &mainWindow,
		Title:    "Mini Transmisor DLNA Lite",
		MinSize:  Size{Width: 450, Height: 350},
		Layout:   VBox{Margins: Margins{Top: 10, Bottom: 10, Left: 10, Right: 10}},
		Children: []Widget{
			// Sección 1: Selección de carpeta
			Composite{
				Layout: HBox{MarginsZero: true},
				Children: []Widget{
					PushButton{
						Text: "📁 Seleccionar Carpeta",
						OnClicked: func() {
							dlg := new(walk.FileDialog)
							dlg.Title = "Selecciona la carpeta con tus videos"
							if ok, _ := dlg.ShowBrowseFolder(mainWindow); ok {
								carpetaSeleccionada = dlg.FilePath
								lblCarpeta.SetText(filepath.Base(carpetaSeleccionada))
								
								// Actualizar lista de videos
								videos := listarVideos(carpetaSeleccionada)
								_ = videoModel.SetPublishingActions(false)
								videoModel.SetItems(videos)
								
								// Iniciar servidor HTTP si no está corriendo
								if !servidorIniciado && len(videos) > 0 {
									iniciarServidorWeb(carpetaSeleccionada)
								}
								btnTransmitir.SetEnabled(len(videos) > 0)
							}
						},
					},
					Label{
						AssignTo:  &lblCarpeta,
						Text:      "Ninguna carpeta seleccionada",
						TextColor: walk.RGB(100, 100, 100),
					},
				},
			},
			VSpacer{Size: 10},
			// Sección 2: Lista de videos
			Label{Text: "Selecciona un video para transmitir a tu TV:"},
			ListBox{
				AssignTo: &lbVideos,
				Model:    videoModel,
			},
			VSpacer{Size: 10},
			// Sección 3: Botón de acción
			PushButton{
				AssignTo: &btnTransmitir,
				Text:     "📺 Transmitir por DLNA a la TV",
				Enabled:  false,
				OnClicked: func() {
					idx := lbVideos.CurrentIndex()
					if idx < 0 {
						walk.MsgBox(mainWindow, "Atención", "Por favor, selecciona un video de la lista.", walk.MsgBoxIconWarning)
						return
					}
					
					items := videoModel.Items()
					videoSeleccionado := items[idx].(string)
					urlVideo := fmt.Sprintf("http://%s:%s/%s", ipLocal, puerto, videoSeleccionado)

					// Lanzar protocolo de descubrimiento SSDP de fondo
					go lanzarAnuncioSSDP(ipLocal, puerto, videoSeleccionado)

					mensaje := fmt.Sprintf("🚀 Transmitiendo servicio DLNA...\n\n🔗 Enlace: %s\n\nVe a tu Smart TV y abre el 'Reproductor Multimedia' o la sección de 'Dispositivos de Red' para ver el video.", urlVideo)
					walk.MsgBox(mainWindow, "Streaming Activo", mensaje, walk.MsgBoxIconInformation)
				},
			},
		},
	}.Create()

	if err != nil {
		panic(err)
	}

	mainWindow.Run()
}

func listarVideos(ruta string) []string {
	var videos []string
	archivos, err := os.ReadDir(ruta)
	if err != nil {
		return videos
	}
	for _, archivo := range archivos {
		ext := strings.ToLower(filepath.Ext(archivo.Name()))
		if ext == ".mp4" || ext == ".mkv" || ext == ".avi" {
			videos = append(videos, archivo.Name())
		}
	}
	return videos
}

func iniciarServidorWeb(ruta string) {
	fs := http.FileServer(http.Dir(ruta))
	http.Handle("/", fs)
	go func() {
		_ = http.ListenAndServe(":"+puerto, nil)
	}()
	servidorIniciado = true
}

func lanzarAnuncioSSDP(ip, puerto, video string) {
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
