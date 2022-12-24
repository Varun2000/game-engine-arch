# Project Setup

I already included the necessary executable files based ./client in 'game-client' folder and ./server in 'game-server' folder which was already built in Linux (WSL) platform. 

In case this fails, you can use the following steps to set it up properly:-

## Prerequisites

Need to install cmake version 3.5

## Procedure

1)Execute the command 'cmake .' in 'server' folder and then run 'make' command.
2) Run 'make' command in 'client' folder
3) It will generate the executable files './main' and './server'.

# Run Client Program

To run the client you have to run the command in this manner './main 0', where 0 is client id for the process.
To run multiple clients, you have to run in following manner './main 1', './main 2'.

# Run Server Program

To run the server program, you have to run the following command in this manner, './server'.

# Run Script 
1) Press Key H to raise an event using scripts
2) Press Key Space + Key K to modify the game object in the game scene.

# Car Game
1) Use Arrow keys to move the player object

# Space Shooter
1) Use left and right arrow keys to move the shooter object
2) Press Key U to shoot Bullet objects

# Ping Pong
1) Use left and right arrow keys to move bat object