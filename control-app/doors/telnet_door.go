package doors

import (
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"time"
)

type TelnetDoor struct {
	Door
	ip       string
	password string
	conn     net.Conn
}

type TelnetDoorTransaction struct {
	FobTransaction
}

func NewTelnetDoor(ip string, password string) (*TelnetDoor, error) {
	d := &TelnetDoor{
		ip:       ip,
		password: password,
	}
	return d, d.connect()
}

func (d *TelnetDoor) connect() error {
	conn, err := net.DialTimeout("tcp", fmt.Sprintf("%s:23", d.ip), time.Second*5)
	if err != nil {
		return err
	}
	d.conn = conn
	go func() {
		buf := make([]byte, 4096)
		currentLine := ""
		for {
			n, err := conn.Read(buf)
			if err != nil {
				if errors.Is(err, io.EOF) {
					log.Println("Door disconnected")
					os.Exit(0)
					return
				}
				log.Println("Error reading from door: ", err)
				os.Exit(1)
				return
			}
			for i := range n {
				c := buf[i]
				if c == '\n' {
					log.Println("From door: ", currentLine)
					currentLine = ""
				} else {
					currentLine += string(c)
				}
			}
		}
	}()
	return nil
}

func (d *TelnetDoor) Close() error {
	return d.conn.Close()
}

func (d *TelnetDoor) protoLogin() error {
	_, err := d.conn.Write([]byte(fmt.Sprintf("L:\nL:%s\n", d.password)))
	return err
}

func (d *TelnetDoor) protoOpen() error {
	_, err := d.conn.Write([]byte("O:\n"))
	return err
}

func (d *TelnetDoor) protoWriteFobs() error {
	_, err := d.conn.Write([]byte("W:\n"))
	return err
}

func (d *TelnetDoor) Open() error {
	if err := d.protoLogin(); err != nil {
		return fmt.Errorf("failed login: %w", err)
	}
	if err := d.protoOpen(); err != nil {
		return fmt.Errorf("failed open: %w", err)
	}
	return nil
}

func (d *TelnetDoor) ProgramFob(number string, allowAccess bool) error {
	txn, err := d.NewTransaction()
	if err != nil {
		return err
	}
	err = txn.ProgramFob(number, allowAccess)
	if err != nil {
		return err
	}
	return d.WriteTransaction(txn)
}

func (d *TelnetDoor) WipeFobs() error {
	txn, err := d.NewTransaction()
	if err != nil {
		return err
	}
	err = txn.WipeFobs()
	if err != nil {
		return err
	}
	return d.WriteTransaction(txn)
}

func (d *TelnetDoor) NewTransaction() (FobTransaction, error) {
	return NewTelnetTransaction()
}

func (d *TelnetDoor) WriteTransaction(txn FobTransaction) error {
	if txn2, ok := txn.(*TelnetTransaction); !ok {
		return errors.New("wrong transaction type")
	} else {
		if err := d.protoLogin(); err != nil {
			return fmt.Errorf("failed login: %w", err)
		}
		if _, err := d.conn.Write([]byte(txn2.buf)); err != nil {
			return fmt.Errorf("failed writing to door: %w", err)
		}
		if err := d.protoWriteFobs(); err != nil {
			return fmt.Errorf("failed committing fobs: %w", err)
		}
	}
	return nil
}

type TelnetTransaction struct {
	FobTransaction
	buf string
}

func NewTelnetTransaction() (FobTransaction, error) {
	return &TelnetTransaction{
		buf: "",
	}, nil
}

func (t *TelnetTransaction) ProgramFob(number string, allowAccess bool) error {
	if !allowAccess {
		return nil // we can't unprogram fobs yet
	}
	t.buf += fmt.Sprintf("F:%s\n", number)
	return nil
}

func (t *TelnetTransaction) WipeFobs() error {
	t.buf += "C:\n"
	return nil
}
