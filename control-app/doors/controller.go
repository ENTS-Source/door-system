package doors

type Config struct {
	IPsToZones map[string]string
}

type Controller interface {
	GetZone(name string) Zone
	GetGlobalZone() Zone
}

type Zone interface {
	OpenDoors() error
	ProgramFob(number string, allowAccess bool) error
	WipeFobs() error
}
