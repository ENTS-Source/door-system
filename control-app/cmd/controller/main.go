package main

import (
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/kelseyhightower/envconfig"
)

type config struct {
	HttpBind string `envconfig:"http_bind" default:"0.0.0.0:8080"`

	ApiSharedKey string `envconfig:"api_shared_key"`

	AmpApiKey       string `envconfig:"amp_api_key"`
	AmpApiKeyFile   string `envconfig:"amp_api_key_file"`
	AmpApiUrl       string `envconfig:"amp_api_url"`
	AmpCategoryId   int    `envconfig:"amp_category_id"`
	AmpBufferDays   int    `envconfig:"amp_buffer_days" default:"3"`
	AmpResyncOnHour int    `envconfig:"amp_resync_on_hour" default:"4"`

	DbPath string `envconfig:"db_path" default:"./controller.db"`

	Doors map[string]string `envconfig:"doors"` // IP to zone name
}

func main() {
	var c config
	err := envconfig.Process("dc", &c)
	if err != nil {
		log.Fatal(err)
	}

	timer := scheduleResync(c)

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	go func() {
		defer close(stop)
		<-stop

		log.Println("Stopping timer...")
		timer.Stop()

		log.Println("Stopping doors...")
		doors.Disconnect()

		log.Println("Stopping api...")
		api.Stop()

		log.Println("Stopping matrix...")
		matrix.Stop()

		log.Println("Stopping database...")
		db.Stop()

		log.Println("Cleaning up...")
		_ = os.RemoveAll(webPath)

		log.Println("Done stopping")
	}()

	wg.Add(1)
	wg.Wait()

	log.Println("Goodbye!")
}

func scheduleResync(c config) *time.Timer {
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
				amember.ResyncAllUsers()
				scheduleResync(c)
			}
		}
	}()
	return timer
}
