package doors

type SingleDoorZone struct {
	Zone
	door Door
}

func NewSingleDoorZone(door Door) (Zone, error) {
	return &SingleDoorZone{
		door: door,
	}, nil
}

func (z *SingleDoorZone) OpenDoors() error {
	return z.door.Open()
}

func (z *SingleDoorZone) ProgramFob(fob string, access bool) error {
	return z.door.ProgramFob(fob, access)
}

func (z *SingleDoorZone) WipeFobs() error {
	return z.door.WipeFobs()
}

func (z *SingleDoorZone) NewTransaction() (FobTransaction, error) {
	return z.door.NewTransaction()
}

func (z *SingleDoorZone) WriteTransaction(txn FobTransaction) error {
	return z.door.WriteTransaction(txn)
}
