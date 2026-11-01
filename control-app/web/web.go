package web

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/ents-source/door-system/doors"
	"github.com/ents-source/go-amember-api/amember"
)

type Config struct {
	BindAddress          string
	SelfApiKey           string
	AccessBufferDays     int
	AmpApiKey            string
	AmpApiUrl            string
	AmpProductCategoryId int
}

type Server interface {
	Start() error
	Stop() error
	ResyncAllUsers()
}

type impl struct {
	config Config
	doors  doors.Controller
	ampApi *amember.Client
	srv    *http.Server
}

func NewServer(config Config, doors doors.Controller) Server {
	ampApi := amember.NewClient(config.AmpApiKey, config.AmpApiUrl)
	return &impl{
		config: config,
		doors:  doors,
		ampApi: ampApi,
	}
}

func (s *impl) requireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != fmt.Sprintf("Bearer %s", s.config.SelfApiKey) {
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		next(w, r)
	}
}

func (s *impl) Start() error {
	http.HandleFunc("/v1/amember", s.doWebhook)
	http.HandleFunc("/v1/resync", s.requireAuth(s.doResync))
	http.HandleFunc("/v1/doors/open", s.requireAuth(s.doOpenAllDoors))
	http.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	})
	s.srv = &http.Server{Addr: s.config.BindAddress}
	return s.srv.ListenAndServe()
}

func (s *impl) Stop() error {
	if s.srv == nil {
		return nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	return s.srv.Shutdown(ctx)
}
