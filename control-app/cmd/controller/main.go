package main

import (
	"log"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/ents-source/door-system/doors"
	"github.com/ents-source/door-system/web"
	"github.com/kelseyhightower/envconfig"
)

type config struct {
	HttpBind string `envconfig:"http_bind" default:"0.0.0.0:8080"`

	ApiSharedKey string `envconfig:"api_shared_key"`

	AmpApiKey string `envconfig:"amp_api_key"`
	//AmpApiKeyFile   string `envconfig:"amp_api_key_file"` // TODO: Restore
	AmpApiUrl       string `envconfig:"amp_api_url"`
	AmpCategoryId   int    `envconfig:"amp_category_id"`
	AmpBufferDays   int    `envconfig:"amp_buffer_days" default:"3"`
	AmpResyncOnHour int    `envconfig:"amp_resync_on_hour" default:"4"`

	DoorIp       string `envconfig:"door_ip"`
	DoorPassword string `envconfig:"door_password"`
}

func init() {
	log.SetOutput(os.Stdout)
	log.SetPrefix("[door-system] ")
	log.SetFlags(log.LstdFlags | log.Lshortfile | log.Lmicroseconds)
}

func main() {
	var c config
	err := envconfig.Process("dc", &c)
	if err != nil {
		log.Fatal(err)
	}

	log.Println("Setting up door...")
	door, err := doors.NewTelnetDoor(c.DoorIp, c.DoorPassword)
	if err != nil {
		log.Fatal(err)
	}
	defer door.Close()
	zone, err := doors.NewSingleDoorZone(door)
	if err != nil {
		log.Fatal(err)
	}
	controller, err := doors.NewSingleDoorController(zone)
	if err != nil {
		log.Fatal(err)
	}

	log.Println("Setting up web api...")
	webApi := web.NewServer(web.Config{
		BindAddress:          c.HttpBind,
		SelfApiKey:           c.ApiSharedKey,
		AccessBufferDays:     c.AmpBufferDays,
		AmpApiKey:            c.AmpApiKey,
		AmpApiUrl:            c.AmpApiUrl,
		AmpProductCategoryId: c.AmpCategoryId,
	}, controller)
	go func() {
		log.Println("Starting web api...")
		if err = webApi.Start(); err != nil {
			log.Fatal(err)
		}
	}()

	log.Println("Starting resync timer...")
	timer := scheduleResync(c, webApi)

	log.Println("Ready! Waiting for exit")
	wg := &sync.WaitGroup{}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	go func() {
		defer close(stop)
		<-stop

		log.Println("Stopping timer...")
		timer.Stop()

		log.Println("Stopping web...")
		webApi.Stop()

		log.Println("Done stopping")
		wg.Done()
	}()

	wg.Add(1)
	wg.Wait()

	log.Println("Goodbye!")
}

func scheduleResync(c config, webApi web.Server) *time.Timer {
	now := time.Now()
	target := time.Date(now.Year(), now.Month(), now.Day(), c.AmpResyncOnHour, 0, 0, 0, time.Local)
	if target.Before(now) {
		target = target.Add(24 * time.Hour)
	}
	log.Println(target.String())
	timer := time.NewTimer(time.Until(target))
	go func() {
		for {
			select {
			case <-timer.C:
				log.Println("Resyncing all users on timer")
				webApi.ResyncAllUsers()
				scheduleResync(c, webApi)
			}
		}
	}()
	return timer
}
