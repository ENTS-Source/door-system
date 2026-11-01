package doors

type SingleDoorController struct {
	Controller
	zone Zone
}

func NewSingleDoorController(zone Zone) (Controller, error) {
	return &SingleDoorController{
		zone: zone,
	}, nil
}

func (c *SingleDoorController) GetZone(name string) Zone {
	return c.zone
}

func (c *SingleDoorController) GetGlobalZone() Zone {
	return c.zone
}
