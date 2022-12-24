
//
//  Weather update server in C++
//  Binds PUB socket to tcp://*:5556
//  Publishes random weather updates
//
#include <zmq.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include<iostream>
#include<thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Data{
    float CarPosX;
    float CarPosY;
};

int main () {

    zmq::context_t context (1);
    zmq::context_t context1 (1);

    zmq::socket_t publisher (context, zmq::socket_type::pub);
     publisher.bind("tcp://*:5556");
    
    zmq::socket_t replier (context1, zmq::socket_type::rep);
    replier.bind("tcp://*:5557");

 
        float x,y;
        std::string msgText;
    while (1) {
            
        zmq::message_t request;
        replier.recv(request, zmq::recv_flags::none);

        std::istringstream iss(static_cast<char*>(request.data()));
        iss >> msgText;
        std::cout<<msgText<<std::endl;
        json rObj = json::parse(msgText);
        std::cout << "Received "<< rObj["event"]<<" "<<rObj["PosX"] << " "<<rObj["PosY"] << std::endl;

        replier.send(zmq::buffer("received"), zmq::send_flags::none);
        zmq::message_t message(100);
        std::cout<<"Sent the reply"<<std::endl;
        snprintf ((char *) message.data(), 100 ,"%s",msgText.c_str());

        publisher.send(message, zmq::send_flags::none);
        std::cout<<"Published the positiion"<<std::endl;
        
    }
    

    replier.close();
    publisher.close();



    return 0;
}
