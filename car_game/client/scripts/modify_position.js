// This script moves the GameObject with handle "gameobject0" (it's guid)
function move(x,y) {
	var tx = gameobject2.x;
	var ty = gameobject2.y;
	gameobject2.x = tx + x;
	gameobject2.y = ty + y;
}
print('Enter modify position')
print("Starting position for "+ gameobject2.x + "\nUpdating...");
move(2,-5);
print('update')
print("Ending position for " + gameobject2.guid + ": " + gameobject2.x + ", " + gameobject2.y + "\nDone...");


