package web

import (
	"io"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"sync"
	"time"

	"github.com/ents-source/door-system/doors"
	"github.com/ents-source/go-amember-api/amember"
)

var ProductCategoryId int
var AccessBufferDays int

func (s *impl) doResync(w http.ResponseWriter, r *http.Request) {
	s.ResyncAllUsers()
	w.WriteHeader(http.StatusOK)
}

func (s *impl) doOpenAllDoors(w http.ResponseWriter, r *http.Request) {
	err := s.doors.GetGlobalZone().OpenDoors()
	if err != nil {
		log.Println(err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
	w.WriteHeader(http.StatusOK)
}

func (s *impl) doWebhook(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()
	b, err := io.ReadAll(r.Body)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	vals, err := url.ParseQuery(string(b))
	ev := vals.Get("am-event")
	log.Println("[aMember Pro Webhook]", ev)
	switch ev {
	case "accessAfterInsert":
		fallthrough
	case "accessAfterUpdate":
		fallthrough
	case "accessAfterDelete":
		fallthrough
	case "userAfterInsert":
		fallthrough
	case "userAfterUpdate":
		userId, err := strconv.Atoi(vals.Get("user[user_id]"))
		if err != nil {
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		s.resyncUser(userId)
		w.WriteHeader(http.StatusOK)
		return
	case "userAfterDelete":
		log.Printf("User %s was deleted - they will be caught in the next global sync", vals.Get("user[user_id]"))
	default:
		w.WriteHeader(http.StatusNotFound)
		return
	}
}

func (s *impl) resyncUser(id int) {
	log.Println("Syncing access records for user ID", id)
	user, err := s.ampApi.GetUser(id)
	if err != nil {
		log.Println("[DOOR UPDATE]", err)
		return
	}

	txn, err := s.doors.GetGlobalZone().NewTransaction()
	if err != nil {
		log.Println("[DOOR UPDATE]", err)
		return
	}

	s.updateUserFromRecord(user, false, txn)
	if err = s.doors.GetGlobalZone().WriteTransaction(txn); err != nil {
		log.Println("[DOOR UPDATE]", err)
		return
	}
}

func (s *impl) updateUserFromRecord(user amember.User, skipIfDisabled bool, txn doors.FobTransaction) {
	if user.Fob == "" || user.Fob == "N/A" {
		return // no possible change
	}
	productIds, err := s.ampApi.GetProductIdsForCategory(ProductCategoryId)
	if err != nil {
		log.Println("Error asking for category information", err)
		return
	}
	log.Println("Product Ids for Category", productIds)

	log.Println(user.Id, "access is", user.FobAccess)
	switch user.FobAccess {
	case "enabled":
		err = txn.ProgramFob(user.Fob, true)
	case "disabled":
		err = txn.ProgramFob(user.Fob, false)
	case "subscription":
		fallthrough
	case "":
		// First we need to find an access record for the product category we should be
		// looking for.
		didUpdate := false
		for _, row := range user.Nested.Access {
			usefulRow := false
			for _, pid := range productIds {
				if row.ProductId == pid {
					usefulRow = true
					break
				}
			}
			if !usefulRow {
				continue
			}

			var startDate time.Time
			var endDate time.Time
			startDate, err = time.Parse("2006-01-02", row.BeginDate)
			if err != nil {
				log.Println("Error parsing start date", row.BeginDate, err)
			}
			endDate, err = time.Parse("2006-01-02", row.EndDate)
			if err != nil {
				log.Println("Error parsing end date", row.EndDate, err)
			}
			endDate = endDate.Add(time.Duration(AccessBufferDays) * 24 * time.Hour)

			log.Println("Access range", row.ProductId, startDate, endDate)
			if startDate.Before(time.Now()) && endDate.After(time.Now()) {
				err = txn.ProgramFob(user.Fob, true)
				didUpdate = true
				break
			}
		}
		if !didUpdate && !skipIfDisabled {
			err = txn.ProgramFob(user.Fob, false) // no useful access records
		}
	default:
		log.Println("Unknown FobAccess value", user.FobAccess)
		return
	}

	if err != nil {
		log.Println("Error programming fob", user.Fob, err)
	}
}

var syncMutex = new(sync.Mutex)

func (s *impl) ResyncAllUsers() {
	syncMutex.Lock()
	defer syncMutex.Unlock()
	log.Println("Resyncing all users")

	users, err := s.ampApi.GetAllUsers()
	if err != nil {
		log.Println("Error getting all users for resync: ", err)
		return
	}

	txn, err := s.doors.GetGlobalZone().NewTransaction()
	if err != nil {
		log.Println("Error creating new door transaction: ", err)
		return
	}
	err = txn.WipeFobs()
	if err != nil {
		log.Println("Error wiping fobs (continuing anyway)", err)
	}
	for _, u := range users {
		s.updateUserFromRecord(u, true, txn)
	}
	if err = s.doors.GetGlobalZone().WriteTransaction(txn); err != nil {
		log.Println("Error writing door transaction: ", err)
		return
	}
}
