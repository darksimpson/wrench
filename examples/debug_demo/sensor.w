// A second program for "wrench dbgserver -context sensor.w": its global
// code runs once, then the server calls tick() ten times a second, as
// firmware would.

var readings = 0;
var history[] = { 0, 0, 0, 0 };

function tick()
{
	readings += 1;
	var value = (readings * 7) % 23;
	history[readings % 4] = value;
	return value;
}
