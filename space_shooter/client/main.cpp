#include <sstream>
#include <cstdlib>
#include <SFML/Graphics.hpp>
#include "ScriptManager.h"
#include <v8.h>
#include <libplatform/libplatform.h>
#include "v8helpers.h"
#include <cstdio>
#include "GameObject.h"
#include <zmq.hpp>
#include <iostream>
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
    Death,
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

class Bullet : public GameObject
{
private:
    int xDir = 1;
    int yDir = 1;
    float xVelocity = .5f;
    float yVelocity = -.5f;

public:
    Bullet(sf::Vector2f shape, sf::Vector2f position, sf::Color color) : GameObject(shape, position, NULL, color)
    {
    }

    void reboundSides()
    {
        xVelocity = -xVelocity;
        xDir = -xDir;
    }

    void reboundBatOrTop()
    {
        this->y -= (yVelocity * 30);
        yVelocity = -yVelocity;
        yDir = -yDir;
    }
    void hitBottom()
    {
        this->y = 1;
        this->x = 500;
    }

    void update(float speed)
    {
        // Update the ball position variables
        this->y -= speed * 40;

        // Move the ball and the bat
        this->updateObjPosition(sf::Vector2f(this->x, this->y));
    }
};

class Shooter : public GameObject
{

public:
    Shooter(sf::Vector2f shape, sf::Vector2f position, sf::Color color) : GameObject(shape, position, NULL, color)
    {
    }
};

class Asteroid : public GameObject
{
public:
    Asteroid(sf::Vector2f shape, sf::Vector2f position, sf::Color color) : GameObject(shape, position, NULL, color)
    {
    }
};

std::deque<Bullet> bullets;

class GameScene
{
public:
    int score;
    NetworkHandler *nh;
    Timeline *t;
    Shooter *bat;
    Asteroid *ast;
};
GameScene *gS;

class EventManager
{ // Class to handle Events
private:
    std::map<EventType, std::function<void(Event, GameScene *)>> _eventReg;

public:
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

        switch (e.eT)
        {
        case CarMovement:
            _eventReg[EventType::CarMovement](e, gameS);
            break;

        case Death:
            _eventReg[EventType::Death](e, gameS);
            break;

        case DisplayTime:
            std::cout << "Entered DisplayTime " << std::endl;
            handle_script = true;
            break;
        }
    }
};

std::priority_queue<Event, std::vector<Event>, compareTimeStamps> EventManager::eventQueue;

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

void sendUptMovePosition(Event e, GameScene *gs)
{ // Handle event for Player movement event
    json obj = createJsonObject(e);
    gs->nh->sendMessage(obj);
}

void moveBatUsingThread(sf::RenderWindow *window, GameScene *gS, EventManager *em)
{

    Shooter *bat = gS->bat;
    NetworkHandler *nh = gS->nh;
    Event e;
    e.eT = CarMovement;
    while (window->isOpen())
    {

        float dist = 60 * gameTime;

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left) && window->hasFocus()) // Press Left Arrow to move left
        {

            sf::Vector2f batPos = bat->getObjPosition() + sf::Vector2f(-dist, 0.f);
            if (batPos.x > 0)
            {
                e.d.posX = batPos.x;
                e.d.posY = batPos.y;
                em->raiseEvent(e);
            }
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right) && window->hasFocus()) // Press Right Arrow to move right
        {

            sf::Vector2f batPos = bat->getObjPosition() + sf::Vector2f(dist, 0.f);
            if (batPos.x < window->getSize().x)
            {
                e.d.posX = batPos.x;
                e.d.posY = batPos.y;
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
                gs->bat->updateObjPosition(sf::Vector2f(obj["PosX"], obj["PosY"]));
            break;
        default:
            std::cout << "Not found Error !!" << std::endl;
        }
        // std::cout<<"Received data : "<<" "<<xpos<<" "<<ypos<<std::endl;
    }
}
void onCollision(Event e, GameScene *gs)
{

    gs->score++;
    bullets.clear();
    std::cout << "Entered collision " << e.d.posX << " " << e.d.posY << std::endl;
    gs->ast->updateObjPosition(sf::Vector2f(e.d.posX, e.d.posY));
}

int main(int argc, char **argv)
{

    XInitThreads();
    srand(time(0));
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.release());
    v8::V8::InitializeICU();
    v8::V8::Initialize();
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate *isolate = v8::Isolate::New(create_params);
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

        subscriber.connect("tcp://localhost:5558");

        zmq::socket_t requester(context1, zmq::socket_type::req);
        requester.connect("tcp://localhost:5559");

        NetworkHandler nh(&requester, &subscriber);

        int windowWidth = 900;
        int windowHeight = 900;

        sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Space Shooter");
        auto desktop = sf::VideoMode::getDesktopMode();
        window.setPosition(sf::Vector2i(desktop.width / 2 - window.getSize().x / 2, desktop.height / 2 - window.getSize().y / 2));
        window.setFramerateLimit(60);
        int score = 0;
        // int lives = 3;

        Shooter bat(sf::Vector2f(20, 60), sf::Vector2f(windowWidth / 2, windowHeight - 65), sf::Color(sf::Color::Magenta));
        bat.exposeToV8(isolate, object_context);

        Asteroid ast(sf::Vector2f(60, 60), sf::Vector2f(windowWidth / 2 - 100, windowHeight / 2), sf::Color(sf::Color::Green));

        sf::Text hud;

        sf::Font font;
        font.loadFromFile("LEMONMILK-Regular.otf");
        hud.setFont(font);
        hud.setCharacterSize(45);
        hud.setFillColor(sf::Color::White);

        Timeline timeObj;

        GameScene gs;

        gs.bat = &bat;
        gs.nh = &nh;
        gs.t = &timeObj;
        gs.score = 0;
        gs.ast = &ast;
        gS = &gs;
        EventManager em(&gs);
        em.registerEvents(EventType::CarMovement, &sendUptMovePosition); // Registering each event type to static functions
        em.registerEvents(EventType::Death, &onCollision);

        sf::Text t;
        t.setFont(font);
        t.setCharacterSize(24);
        t.setFillColor(sf::Color::Red);
        t.setStyle(sf::Text::Bold);
        t.setPosition(sf::Vector2f(600, 40));

        std::thread batMove(std::bind(&moveBatUsingThread, &window, &gs, &em));
        std::thread receiveMsg(std::bind(&receiveMessage, &window, &subscriber, &gs));
        std::thread eventHandler(std::bind(&handleAllEvents, &window, &em));

        while (window.isOpen())
        {

            sf::Event event;
            if (!timeObj.getGamePauseStatus())
                gameTime = timeObj.getUpdatedTime();

            sm->reloadAll();
            sm->reloadAll("object_context");

            while (window.pollEvent(event))
            {
                if (event.type == sf::Event::Closed)

                    window.close();

                // Making use of Chord
                if (event.type == sf::Event::KeyPressed)
                {
                    if (event.key.code == sf::Keyboard::K && sf::Keyboard::isKeyPressed(sf::Keyboard::Space) && window.hasFocus())
                    {
                        // Modifing Roadblock GameObject position
                        std::cout << bat.guid << std::endl;
                        sf::Vector2f pos = bat.getObjPosition();
                        bat.x = pos.x;
                        bat.y = pos.y;

                        sm->runOne("modify_position", true, "object_context");

                        bat.updateObjPosition(sf::Vector2f(bat.x, bat.y));
                    }

                    else if (event.key.code == sf::Keyboard::U)
                    {
                        std::cout << "Creating Ball " << bullets.size() << std::endl;
                        sf::Vector2f pos = bat.getObjPosition();
                        Bullet b(sf::Vector2f(5, 20), sf::Vector2f(pos.x + 10, pos.y + 20), sf::Color(sf::Color::Red));
                        bullets.push_back(b);
                    }

                    else if(event.key.code == sf::Keyboard::H)
                        sm->runOne("raise_event", true);

                    else if (event.key.code == sf::Keyboard::K && window.hasFocus())
                    { // 2 time scale
                        timeObj.setTimeScale(2);
                    }
                }
            }

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Escape))
            {

                window.close();
            }

            // Handle ball hitting top
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::L) && window.hasFocus())
            { // 1 time scale
                timeObj.setTimeScale(1);
            }



            for (int i = 0; i < bullets.size(); i++)
            {
                if (bullets[i].getObjGlobalBounds().intersects(ast.getObjGlobalBounds()))
                { // Collision Detection

                    sf::Vector2f pos = sf::Vector2f(100 + rand() % 700, 400 + rand() % 500);
                    Event e;
                    e.d.posX = pos.x;
                    e.d.posY = pos.y;
                    e.eT = Death;
                    em.raiseEvent(e);
                }
                else if (bullets[i].getObjPosition().y < 0)
                {
                    std::deque<Bullet>::iterator itr = bullets.begin();
                    std::cout << "Deleting Ball " << i << std::endl;
                    bullets.erase(itr + i);
                }
            }

            if (handle_script)
            {
                handle_script = false;
                sm->runOne("handle_event", true);
            }

            for (int i = 0; i < bullets.size(); i++)
                bullets[i].update(gameTime);

            std::stringstream ss;
            ss << "Score:" << gs.score;
            hud.setString(ss.str());

            window.clear();

            if (disp_time != "")
            {
                t.setString(disp_time);
                window.draw(t);
            }

            bat.drawObj(&window);

            for (int i = 0; i < bullets.size(); i++)
                bullets[i].drawObj(&window);

            ast.drawObj(&window);
            // Draw our score
            window.draw(hud);

            // Show everything we just drew
            window.display();
        }
        batMove.detach();
        receiveMsg.detach();
        eventHandler.detach();

        subscriber.close();
        requester.close();
    }

    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    return 0;
}