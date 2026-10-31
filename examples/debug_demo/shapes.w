struct Box( w, h )
{
	var width = w;
	var height = h;
};

function boxArea( b )
{
	var area = b.width * b.height;
	return area;
}
