#include "ScriptManager.h"
#include <v8.h>
#include <libplatform/libplatform.h>
#include <iostream>
#include "v8helpers.h"
#include <cstdio>
#include "GameObject.h"
#include <SFML/Graphics.hpp>
#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <future>
#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <queue>
#include <X11/Xlib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
int client_id = -1;
bool handle_script = false;
std::string disp_time;
enum EventType
{ // Event Representation
    CarMovement = 0,
    ObstMovement,
    Spawn,
    Death,
    RecordingStart,
    RecordingEnd,
    DisplayTime
};
float gameTime;
struct Data
{
    float posX;
    float posY;
};

struct Event
{
    EventType eT;
    Data d;
    long timestamp;
};

json createJsonObject(Event dat)
{
    json obj;
    obj["clientid"] = client_id;
    obj["event"] = dat.eT;
    obj["PosX"] = dat.d.posX;
    obj["PosY"] = dat.d.posY;
    return obj;
}

class NetworkHandler
{
private:
    zmq::socket_t *requester;
    zmq::socket_t *subscriber;

public:
    NetworkHandler(zmq::socket_t *req, zmq::socket_t *sub)
    {
        requester = req;
        subscriber = sub;
    }

    void sendMessage(json obj)
    {
        zmq::message_t message(100);

        snprintf((char *)message.data(), 100, "%s", obj.dump().c_str());

        requester->send(message, zmq::send_flags::none);
        zmq::message_t reply{};
        requester->recv(reply, zmq::recv_flags::none);
    }
};

class Timeline
{

    sf::Clock gameTime;
    sf::Clock loopTime;
    static sf::Clock gT;

private:
    float timeScale;
    bool isGamePaused;

public:
    Timeline()
    {
        timeScale = 1;
        isGamePaused = false;
    }

    float getGameTime()
    {
        return gameTime.getElapsedTime().asSeconds();
    }

    float getLoopTime()
    {
        return loopTime.restart().asSeconds();
    }

public:
    static float getGT()
    {
        return gT.getElapsedTime().asSeconds();
    }

    char *getRealTime()
    {
        time_t now = time(0);
        char *date_time = ctime(&now);
        return date_time;
    }

    void setGamePause()
    {
        isGamePaused = true;
    }

    void setGameUnPause()
    {
        isGamePaused = false;
        loopTime.restart();
    }

    void setTimeScale(float s)
    {
        timeScale = s;
    }

    bool getGamePauseStatus()
    {
        return isGamePaused;
    }

    float getUpdatedTime()
    {
        return getLoopTime() * timeScale;
    }
};

sf::Clock Timeline::gT;

struct compareTimeStamps
{
    bool operator()(Event const &e1, Event const &e2)
    {
        return e1.timestamp > e2.timestamp;
    }
};

class FootPath : public GameObject
{ // FootPath Class extends indirectly from RectangleShape
private:
    sf::FloatRect boundingBox;

public:
    FootPath(sf::Vector2f shape, sf::Vector2f position, sf::Texture *texture) : GameObject(shape, position, texture, sf::Color(sf::Color::Transparent))
    {
        boundingBox = getObjGlobalBounds();
    }

    bool checkCollision(sf::FloatRect shape1, sf::Vector2f directionVector)
    {

        sf::Vector2f edgePoint = directionVector + sf::Vector2f(shape1.width, shape1.height);
        if (boundingBox.contains(directionVector) || boundingBox.contains(edgePoint))
        {
            return true;
        }
        return false;
    }
};

class MyCar_Character : public GameObject
{ // Character Car Class extends indirectly from RectangleShape
public:
    MyCar_Character(sf::Vector2f carSize, sf::Vector2f origin, sf::Texture *texture) : GameObject(carSize, origin, texture, sf::Color(sf::Color::Transparent))
    {
    }
};

class VCarObstacle : public GameObject
{
public:
    sf::FloatRect boundingBox;
    // Movable Character Class extends indirectly from RectangleShape
    VCarObstacle(sf::Vector2f objectSize, sf::Vector2f origin, sf::Color carColor) : GameObject(objectSize, origin, NULL, carColor)
    {
    }

    bool checkCollision(sf::FloatRect otherBox)
    {
        boundingBox = getObjGlobalBounds();
        if (boundingBox.intersects(otherBox))
        {
            return true;
        }

        return false;
    }
};

class SpawnPoint : public GameObject
{
public:
    std::thread deathZone;
    SpawnPoint(sf::Vector2f shape, sf::Vector2f position, sf::Texture *texture) : GameObject(shape, position, texture, sf::Color(sf::Color::Transparent))
    {
    }

    sf::Vector2f getSpawnPoint()
    {
        return getObjPosition();
    }

    void moveObjToSpawn(float x, float y)
    {
        updateObjPosition(sf::Vector2f(x, y));
    }
};

class RoadBlock : public FootPath
{
public:
    RoadBlock(sf::Vector2f shape, sf::Vector2f position, sf::Texture *texture) : FootPath(shape, position, texture)
    {
    }
};

class GameScene
{
public:
    sf::RenderWindow *window;
    std::vector<FootPath *> f;
    MyCar_Character *player;
    std::vector<VCarObstacle *> ob1;
    RoadBlock *rb;
    std::vector<SpawnPoint *> sp;
    NetworkHandler *nh;
    Timeline *t;
    ScriptManager *sm;
    sf::Text *timeDisp;
};
GameScene *gS;

class EventManager
{ // Class to handle Events
private:
    std::map<EventType, std::function<void(Event, GameScene *)>> _eventReg;
    std::vector<Event> replayStack;

public:
    bool eventPaused = false;
    bool isRecording = false;
    GameScene *gameS;

    static std::priority_queue<Event, std::vector<Event>, compareTimeStamps> eventQueue;
    EventManager(GameScene *g)
    {
        gameS = g;
    }

    void registerEvents(EventType e, std::function<void(Event, GameScene *)> f)
    {
        _eventReg[e] = f;
    }

    void raiseEvent(Event event)
    {
        event.timestamp = gameS->t->getGameTime();
        eventQueue.push(event);
    }

    static void raiseScriptEvents(const v8::FunctionCallbackInfo<v8::Value> &args)
    {

        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope scope(isolate);
        v8::Handle<v8::Value> value = args[0];
        v8::Local<v8::Object> json = isolate->GetCurrentContext()->Global()->Get(v8::String::NewFromUtf8(isolate, "JSON"))->ToObject();
        v8::Local<v8::Function> stringify = json->Get(v8::String::NewFromUtf8(
                                                          isolate, "stringify"))
                                                .As<v8::Function>();

        v8::Local<v8::Value> result = stringify->Call(json, 1, &value);
        v8::String::Utf8Value const str(result);

        nlohmann::json j = nlohmann::json::parse(*str);

        Event e;
        e.eT = j["EventType"];
        e.d.posX = j["DataX"];
        e.timestamp = Timeline::getGT();
        eventQueue.push(e);
    }

    void handleGameEvents(Event e)
    {
        if (this->isRecording && e.eT != EventType::RecordingEnd && e.eT != EventType::RecordingStart)
            replayStack.push_back(e);

        switch (e.eT)
        {
        case CarMovement:
            _eventReg[EventType::CarMovement](e, gameS);
            break;
        case ObstMovement:
            _eventReg[EventType::ObstMovement](e, gameS);
            break;
        case Death:
        {
            _eventReg[EventType::Death](e, gameS);
            Event ev;
            ev.eT = Spawn;
            sf::Vector2f point = gameS->sp[0]->getSpawnPoint();
            ev.d.posX = point.x;
            ev.d.posY = point.y;
            this->raiseEvent(ev);
            break;
        }
        case Spawn:
            _eventReg[EventType::Spawn](e, gameS);
            break;
        case DisplayTime:
            std::cout << "Entered DisplayTime " << std::endl;
            handle_script = true;
            break;
        case RecordingStart:
        {
            this->isRecording = true;
            break;
        }
        case RecordingEnd:
        {
            this->isRecording = false;
            this->eventPaused = true;
            std::thread replay(std::bind(&EventManager::replayEvents, this));
            replay.detach();
            break;
        }
        }
    }

    void replayEvents()
    { // Method to start Playback
        std::cout << "Entering Replay Events " << std::endl;
        for (auto e : replayStack)
        {
            switch (e.eT)
            {
            case EventType::CarMovement:
                gameS->player->updateObjPosition(sf::Vector2f(e.d.posX, e.d.posY));
                break;
            case EventType::ObstMovement:
                gameS->ob1[0]->updateObjPosition(sf::Vector2f(e.d.posX, e.d.posY));
                break;
            case EventType::Death:
                std::cout << "Replay:: Death Occured !" << std::endl;
                break;
            case EventType::Spawn:
                std::cout << "Replay:: Spawning Object !" << std::endl;
                gameS->player->updateObjPosition(sf::Vector2f(e.d.posX, e.d.posY));
                break;
            default:
                std::cout << "Doesn't Exist" << e.eT << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        this->replayStack.clear();
        this->eventPaused = false;
    }
};

std::priority_queue<Event, std::vector<Event>, compareTimeStamps> EventManager::eventQueue;

class DeathZone : public GameObject
{
private:
    sf::FloatRect boundingBox;

public:
    std::thread deathZoneThread;
    DeathZone(sf::Vector2f shape, sf::Vector2f position, sf::Texture *texture) : GameObject(shape, position, texture, sf::Color(sf::Color::Transparent))
    {
        boundingBox = getObjGlobalBounds();
    }
    void updateCarIfColided(MyCar_Character *carPlayer, SpawnPoint *sp, sf::RenderWindow *window, EventManager *em)
    {
        Event e;
        e.eT = Death;
        sf::Vector2f carPoint;
        while (window->isOpen())
        {
            if (boundingBox.intersects(carPlayer->getObjGlobalBounds()))
            {
                carPoint = carPlayer->getObjPosition();
                e.d.posX = carPoint.x;
                e.d.posY = carPoint.y;
                em->raiseEvent(e);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
};

void handleScriptEvent(const v8::FunctionCallbackInfo<v8::Value> &args)
{
    v8::Isolate *isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);
    v8::Handle<v8::Value> value = args[0];
    v8::Local<v8::Object> json = isolate->GetCurrentContext()->Global()->Get(v8::String::NewFromUtf8(isolate, "JSON"))->ToObject();
    v8::Local<v8::Function> stringify = json->Get(v8::String::NewFromUtf8(
                                                      isolate, "stringify"))
                                            .As<v8::Function>();

    v8::Local<v8::Value> result = stringify->Call(json, 1, &value);
    v8::String::Utf8Value const str(result);

    std::cout << "Entered Script Handle Method " << std::endl;
    std::cout << "Getting Displayed Value " << *str << " " << std::endl;
    std::string l("Time: ");
    disp_time = l + *str;
}

void handleAllEvents(sf::RenderWindow *window, EventManager *em)
{ // Handling events occurring in the Game Scene

    while (window->isOpen())
    {
        while (gameTime > 0 && !em->eventQueue.empty())
        {
            Event e = em->eventQueue.top();
            em->eventQueue.pop();
            em->handleGameEvents(e);
        }
    }
}

void charDeathOccured(Event e, GameScene *gs)
{ // Handle event for Death event
    std::cout << "Death Occured !!" << std::endl;
}

void sendUptMovePosition(Event e, GameScene *gs)
{ // Handle event for Player and Movable object movement event
    json obj = createJsonObject(e);
    gs->nh->sendMessage(obj);
}

void setCharToSpawn(Event e, GameScene *gs)
{ // Handle event for Spawning event
    std::cout << "Spawning the object " << std::endl;
    gs->player->updateObjPosition(sf::Vector2f(e.d.posX, e.d.posY));
}

void moveObstUsingThread(VCarObstacle *co, sf::RenderWindow *window, EventManager *em)
{
    int sign = 1;
    Event dat;
    dat.eT = ObstMovement;
    while (window->isOpen())
    {
        sf::Vector2f upPos;
        if (window->hasFocus() && !em->eventPaused)
        {
            if (co->getObjPosition().y > 40 && co->getObjPosition().y < (window->getSize().y - 120)) // This code enables the regular pattern for the movable object
            {
                upPos = co->getObjPosition() + sf::Vector2f(0, sign * gameTime * 50);
            }
            else
            {
                sign = -sign;
                sf::Vector2f pos;
                pos.x = co->getObjPosition().x;
                pos.y = co->getObjPosition().y;
                if (pos.y <= 40)
                    upPos = sf::Vector2f(pos.x, 41);
                else
                    upPos = sf::Vector2f(pos.x, window->getSize().y - 121);
            }
            dat.d.posX = upPos.x;
            dat.d.posY = upPos.y;
            em->raiseEvent(dat);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void moveCarUsingThread(sf::RenderWindow *window, GameScene *gS, EventManager *em)
{

    MyCar_Character *carBlock = gS->player;
    FootPath *footpath1 = gS->f.at(0);
    FootPath *footpath2 = gS->f.at(1);
    VCarObstacle *oppCarBlock = gS->ob1.at(0);
    RoadBlock *rb = gS->rb;
    NetworkHandler *nh = gS->nh;
    Event e;
    e.eT = CarMovement;
    while (window->isOpen())
    {

        if (em->eventPaused)
            continue;

        float dist = 50 * gameTime;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left) && window->hasFocus()) // Press Left Arrow to move left
        {
            sf::Vector2f carBoundingPoint = carBlock->getObjPosition();
            carBoundingPoint += sf::Vector2f(-dist, 0.f);
            // Checking collision for two edge points when moving left to make sure character object does not intersect with other objects
            if (!(footpath1->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  footpath2->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  oppCarBlock->checkCollision(carBlock->getObjGlobalBounds()) || rb->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint)))
            {

                sf::Vector2f carPos = carBlock->getObjPosition() + sf::Vector2f(-dist, 0.f);

                e.d.posX = carPos.x;
                e.d.posY = carPos.y;
                em->raiseEvent(e);
            }
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up) && window->hasFocus()) // Press Up Arrow to move up
        {
            sf::Vector2f carBoundingPoint = carBlock->getObjPosition();
            carBoundingPoint += sf::Vector2f(0.f, -dist);
            // Checking collision for two edge points when moving up to make sure character object does not intersect with other objects
            if (!(footpath1->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  footpath2->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  oppCarBlock->checkCollision(carBlock->getObjGlobalBounds()) || rb->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint)))

            {
                sf::Vector2f carPos = carBlock->getObjPosition() + sf::Vector2f(0.f, -dist);
                e.d.posX = carPos.x;
                e.d.posY = carPos.y;

                em->raiseEvent(e);
            }
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down) && window->hasFocus()) // Press Down Arrow to move down
        {
            sf::Vector2f carBoundingPoint = carBlock->getObjPosition();
            carBoundingPoint += sf::Vector2f(0.f, dist);
            // Checking collision for two edge points when moving down to make sure character object does not intersect with other objects
            if (!(footpath1->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  footpath2->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  oppCarBlock->checkCollision(carBlock->getObjGlobalBounds()) || rb->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint)))

            {
                sf::Vector2f carPos = carBlock->getObjPosition() + sf::Vector2f(0.f, dist);
                e.d.posX = carPos.x;
                e.d.posY = carPos.y;

                em->raiseEvent(e);
            }
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right) && window->hasFocus()) // Press Right Arrow to move right
        {
            sf::Vector2f carBoundingPoint = carBlock->getObjPosition();
            carBoundingPoint += sf::Vector2f(dist, 0.f);
            // Checking collision for two edge points when moving right to make sure character object does not intersect with other objects
            if (!(footpath1->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  footpath2->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint) ||
                  oppCarBlock->checkCollision(carBlock->getObjGlobalBounds()) || rb->checkCollision(carBlock->getObjGlobalBounds(), carBoundingPoint)))

            {

                sf::Vector2f carPos = carBlock->getObjPosition() + sf::Vector2f(dist, 0.f);
                e.d.posX = carPos.x;
                e.d.posY = carPos.y;

                em->raiseEvent(e);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void receiveMessage(sf::RenderWindow *window, zmq::socket_t *subscriber, GameScene *gs)
{
    while (window->isOpen())
    {

        std::string msgText;
        zmq::message_t update;
        // std::cout<<"Checking for character Pos"<<std::endl;

        subscriber->recv(update, zmq::recv_flags::none);
        std::istringstream iss(static_cast<char *>(update.data()));
        iss >> msgText;
        json obj = json::parse(msgText);
        EventType eT = obj["event"];
        switch (eT)
        {
        case CarMovement:
            if (obj["clientid"] == client_id)
                gs->player->updateObjPosition(sf::Vector2f(obj["PosX"], obj["PosY"]));
            break;
        case ObstMovement:
            gs->ob1[0]->updateObjPosition(sf::Vector2f(obj["PosX"], obj["PosY"]));
            break;
        default:
            std::cout << "Not found Error !!" << std::endl;
        }
        // std::cout<<"Received data : "<<" "<<xpos<<" "<<ypos<<std::endl;
    }
}

void updateScreenView(sf::RenderWindow *window, MyCar_Character *carBlock)
{

    while (window->isOpen())
    {

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left) && carBlock->getObjPosition().x < 0 && window->hasFocus())
        {
            sf::View current = window->getView();
            // std::cout<<"Entered the method "<<std::endl;
            current.move(sf::Vector2f(-50 * gameTime, 0));
            window->setView(current);
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right) && carBlock->getObjPosition().x > window->getSize().x && window->hasFocus())
        {
            sf::View current = window->getView();
            // std::cout<<"Entered the method "<<std::endl;
            current.move(sf::Vector2f(50 * gameTime, 0));
            window->setView(current);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char **argv)
{

    XInitThreads();

    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.release());
    v8::V8::InitializeICU();
    v8::V8::Initialize();
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate *isolate = v8::Isolate::New(create_params);

    // anonymous scope for managing handle scope
    {
        v8::Isolate::Scope isolate_scope(isolate); // must enter the virtual machine to do stuff
        v8::HandleScope handle_scope(isolate);

        // Best practice to isntall all global functions in the context ahead of time.
        v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
        // Bind the global 'print' function to the C++ Print callback.
        global->Set(isolate, "print", v8::FunctionTemplate::New(isolate, v8helpers::Print));
        // Bind the global static factory function for creating new GameObject instances
        global->Set(isolate, "gameobjectfactory", v8::FunctionTemplate::New(isolate, GameObject::ScriptedGameObjectFactory));
        // Bind the global static function for retrieving object handles
        global->Set(isolate, "gethandle", v8::FunctionTemplate::New(isolate, ScriptManager::getHandleFromScript));
        global->Set(isolate, "raiseEvent", v8::FunctionTemplate::New(isolate, EventManager::raiseScriptEvents));
        global->Set(isolate, "handleEvent", v8::FunctionTemplate::New(isolate, handleScriptEvent));

        v8::Local<v8::Context> default_context = v8::Context::New(isolate, NULL, global);
        v8::Context::Scope default_context_scope(default_context); // enter the context

        ScriptManager *sm = new ScriptManager(isolate, default_context);

        sm->addScript("raise_event", "scripts/raise_event.js");

        // Create a new context
        v8::Local<v8::Context> object_context = v8::Context::New(isolate, NULL, global);
        sm->addContext(isolate, object_context, "object_context");
        sm->addScript("handle_event", "scripts/handle_event.js");
        sm->addScript("modify_position", "scripts/modify_position.js", "object_context");

        client_id = std::stoi(argv[1]);
        zmq::context_t context(1);
        zmq::context_t context1(1);
        //  Socket to talk to server

        zmq::socket_t subscriber(context, zmq::socket_type::sub);
        zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

        subscriber.connect("tcp://localhost:5556");

        zmq::socket_t requester(context1, zmq::socket_type::req);
        requester.connect("tcp://localhost:5557");

        NetworkHandler nh(&requester, &subscriber);

        sf::RenderWindow window(sf::VideoMode(900, 900), "SFML Tutorial", sf::Style::Default);

        window.setFramerateLimit(60);
        auto desktop = sf::VideoMode::getDesktopMode();
        window.setPosition(sf::Vector2i(desktop.width / 2 - window.getSize().x / 2, desktop.height / 2 - window.getSize().y / 2));

        sf::Texture footPathTexture, carTexture;

        if (!footPathTexture.loadFromFile("footpath-texture.jpg"))
            std::cout << "Cannot load the texture !";

        FootPath footpath1(sf::Vector2f(450.f, 10.f), sf::Vector2f(20.f, 200.f), &footPathTexture);

        FootPath footpath2(sf::Vector2f(450.f, 10.f), sf::Vector2f(20.f, 700.f), &footPathTexture);

        RoadBlock rb(sf::Vector2f(40.f, 160.f), sf::Vector2f(200.f, 540.f), &footPathTexture);

        rb.exposeToV8(isolate, object_context);

        if (!carTexture.loadFromFile("car-texture.jpg"))
            std::cout << "Cannot load the texture !";

        MyCar_Character carBlock(sf::Vector2f(80.f, 50.f), sf::Vector2f(30, 390), &carTexture);

        VCarObstacle oppCarBlock(sf::Vector2f(50.f, 80.f), sf::Vector2f(500, 180), sf::Color(245, 34, 56));

        SpawnPoint sp(sf::Vector2f(0, 0), sf::Vector2f(50, 500), NULL);

        DeathZone firstDZ(sf::Vector2f(40.f, 40.f), sf::Vector2f(100, 300), NULL);
        // firstDZ.activateDeathZone(&firstDZ, &carBlock, &sp, &window);

        bool isResizeEnabled = true;
        bool isGamePaused = false;

        sf::Event event;
        Timeline timeObj; // Initialized Timeline Object to track the changes

        sf::Font f;
        if (!f.loadFromFile("LEMONMILK-Regular.otf"))
        {
            std::cout << "Error loading font" << std::endl;
        }

        GameScene gs;
        // Initializing runtime game object model
        gs.f.push_back(&footpath1);
        gs.f.push_back(&footpath2);
        gs.player = &carBlock;
        gs.ob1.push_back(&oppCarBlock);
        gs.rb = &rb;
        gs.sp.push_back(&sp);
        gs.t = &timeObj;
        gs.nh = &nh;
        gs.sm = sm;
        gs.window = &window;

        sf::Text t;
        t.setFont(f);
        t.setCharacterSize(24);
        t.setFillColor(sf::Color::Red);
        t.setStyle(sf::Text::Bold);

        gs.timeDisp = &t;
        gS = &gs;
        EventManager em(&gs);                                            // Initializing Event Management System
        em.registerEvents(EventType::CarMovement, &sendUptMovePosition); // Registering each event type to static functions
        em.registerEvents(EventType::ObstMovement, &sendUptMovePosition);
        em.registerEvents(EventType::Death, &charDeathOccured);
        em.registerEvents(EventType::Spawn, &setCharToSpawn);

        // Starting the thread to handle keyboard movements for car
        std::thread first(std::bind(&moveCarUsingThread, &window, &gs, &em));
        // Starting the thread for movable platform
        std::thread second(std::bind(&moveObstUsingThread, &oppCarBlock, &window, &em));
        // Starting the thread to synchronize movements for car in all clients
        std::thread third(std::bind(&receiveMessage, &window, &subscriber, &gs));
        std::thread fourth(std::bind(&updateScreenView, &window, &carBlock));
        std::thread five(std::bind(&DeathZone::updateCarIfColided, &firstDZ, &carBlock, &sp, &window, &em));
        std::thread seven(std::bind(&handleAllEvents, &window, &em));

        t.setFont(f);
        t.setCharacterSize(24);
        t.setFillColor(sf::Color::Red);
        t.setStyle(sf::Text::Bold);
        bool reload = true;
        while (window.isOpen())
        {

            if (!timeObj.getGamePauseStatus())
                gameTime = timeObj.getUpdatedTime();

            sm->reloadAll();
            sm->reloadAll("object_context");

            while (window.pollEvent(event))
            {

                if (event.type == sf::Event::Closed) // Closes the window upon clicking exit button
                    window.close();

                if (event.type == sf::Event::Resized)
                {
                    std::cout << "Entered the resized method " << isResizeEnabled << "\n";
                    if (!isResizeEnabled) // By default, the game objects resizes proportionately with window size.
                    {
                        sf::FloatRect visibleArea(0.f, 0.f, event.size.width, event.size.height);
                        window.setView(sf::View(visibleArea));
                    }
                }
                if (event.type == sf::Event::KeyPressed)
                {
                    if (event.key.code == sf::Keyboard::H)// Raise Event via Script
                        sm->runOne("raise_event", true);

                    // Making use of Chord
                    if (event.key.code == sf::Keyboard::K && sf::Keyboard::isKeyPressed(sf::Keyboard::Space) && window.hasFocus())
                    {
                        // Modifing Roadblock GameObject position
                        sm->runOne("modify_position", true, "object_context");
                        rb.updateObjPosition(sf::Vector2f(rb.x, rb.y));
                    }
                    else if (event.key.code == sf::Keyboard::K && window.hasFocus())
                    { // 2 time scale
                        timeObj.setTimeScale(2);
                    }
                    else if (event.key.code == sf::Keyboard::J && window.hasFocus())
                    { // 1 time scale
                        timeObj.setTimeScale(1);
                    }
                }
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::S) && window.hasFocus()) // To Enable Scaling
            {
                isResizeEnabled = true;
                std::cout << "Changing Resized to " << isResizeEnabled;
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::C) && window.hasFocus()) // To Disable Scaling Option
            {
                sf::View resizeView = window.getDefaultView();
                window.setView(resizeView);

                isResizeEnabled = false;
                std::cout << "Changing Resized to " << isResizeEnabled;
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::P) && window.hasFocus())
            {
                isGamePaused = !isGamePaused;
                timeObj.setGamePause();
                gameTime = 0;
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Q) && window.hasFocus())
            {
                isGamePaused = !isGamePaused;
                timeObj.setGameUnPause();
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::L) && window.hasFocus())
            { // 0.5 time scale
                timeObj.setTimeScale(0.5);
            }

            if (handle_script)
            {
                handle_script = false;
                sm->runOne("handle_event", true);
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::R) && window.hasFocus())
            { // Press Key 'R' to start Recording the moves
                Event e;
                e.eT = RecordingStart;
                em.raiseEvent(e);
            }
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::T) && window.hasFocus())
            { // Press Key 'T' to end Recording and playback starts
                Event e;
                e.eT = RecordingEnd;
                em.raiseEvent(e);
            }

            window.clear();

            if (disp_time != "")
            {

                t.setString(disp_time);
                window.draw(t);
            }

            footpath1.drawObj(&window);
            footpath2.drawObj(&window);
            carBlock.drawObj(&window);
            oppCarBlock.drawObj(&window);
            firstDZ.drawObj(&window);
            sp.drawObj(&window);
            rb.drawObj(&window);
            window.display();
        }

        first.detach();
        second.detach();
        third.detach();
        fourth.detach();
        five.detach();
        seven.detach();

        subscriber.close();
        requester.close();
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();

    return 0;
}
