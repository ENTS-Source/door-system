package doors

type Controller interface {
	GetZone(name string) Zone
	GetGlobalZone() Zone
}

type Zone interface {
	FobTransaction
	OpenDoors() error
	NewTransaction() (FobTransaction, error)
	WriteTransaction(txn FobTransaction) error
}

type Door interface {
	FobTransaction
	Open() error
	NewTransaction() (FobTransaction, error)
	WriteTransaction(txn FobTransaction) error
}

type FobTransaction interface {
	ProgramFob(number string, allowAccess bool) error
	WipeFobs() error
}
