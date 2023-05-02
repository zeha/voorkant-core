#include <exception>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <poll.h>
#include <nlohmann/json.hpp>

#include <curl/curl.h>
#include "curl/easy.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::map;

using json = nlohmann::json;

// static const uint32_t ID_SUBSCRIPTION = 1;
// static const uint32_t ID_GETSTATES = 2;
// static const uint32_t ID_START = 100;

class HAEntity
{
public:
  HAEntity(void) {

  }
  HAEntity(json _state) {
    state = _state;
  }
  ~HAEntity() {

  }

  void update(json _state) {
    state = _state;
  }

  std::string toString(void) {
    return state.dump(2);
  }

  std::string getState(void) {
    return state["state"];
  }
private:
  json state;
};

map<string, std::shared_ptr<HAEntity>> states;
std::mutex stateslock;

class WSConn
{
public:
  WSConn(std::string url) {
    wshandle = curl_easy_init();

    curl_easy_setopt(wshandle, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(wshandle, CURLOPT_URL, url.c_str());
    curl_easy_perform(wshandle);
  }
  ~WSConn() {
        // FIXME clean up here
  }

  std::string recv(void) {
    size_t recv;
    struct curl_ws_frame *meta;
    struct pollfd pfd;

    char buffer[8192];

    std::string result;

    pfd.events = POLLIN;
        /* cerr<< */ curl_easy_getinfo(wshandle, CURLINFO_ACTIVESOCKET, &pfd.fd) /* <<endl */ ;

    CURLcode ret;

        // FIXME: handle frames > 64k
    while(true) {
      ret = curl_ws_recv(wshandle, buffer, sizeof(buffer), &recv, &meta);
          // cerr<<"CURLE_AGAIN"<<endl;
      // cerr<<"recv ret="<<ret<<endl;
      if (ret == CURLE_OK) {
        std::string chunk(buffer, recv); // FIXME: string_view?
        result = result + chunk;
        // cerr<<"bytesleft="<<(meta->bytesleft)<<endl;
        // cerr<<"result.size()="<<result.size()<<endl;
        if (meta->bytesleft == 0) {
          break;
        }
      }
      else if (ret == CURLE_AGAIN) {
        poll(&pfd, 1, 1000);
      }
      else {
        throw std::runtime_error("got error from curl");
      }
    }
        // cerr<<"ret="<<ret<<endl;
        // cerr<<"buffer="<<buffer<<endl;
        // cerr<<"recv="<<recv<<endl;
        // cout<<"RESULT:"<<endl;
        // cout<<result<<endl;
        // cout<<"END RESULT"<<endl;
    return result;
  }

  void send(std::string& msg) {
    size_t sent;
    curl_ws_send(wshandle, msg.c_str(), msg.length(), &sent, 0, CURLWS_TEXT);
  }

  CURL* wshandle;
};

std::vector<std::string> entries;
std::mutex entrieslock;

ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::FitComponent();

void uithread() {
  using namespace ftxui;

  int selected;
  int selected2;

  // std::vector<std::string> entries2;
  // entries2.push_back("hoi");
  // entries2.push_back("hoi2");

  auto radiobox = Menu(&entries, &selected);
  // auto radiobox2 = Menu(&entries2, &selected2);
  auto renderer = Renderer(radiobox, [&] {
    std::scoped_lock lk(entrieslock, stateslock);

    return vbox({
            hbox(text("selected = "), text(selected >=0 && entries.size() ? entries.at(selected) : "")),
            // hbox(text("selected2 = "), text(selected2 >=0 && entries2.size() ? entries2.at(selected2) : "")),
            vbox(
              {
                hbox(
                  {
                    radiobox->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 15)  | border,
                    vbox(
                      {
                        paragraph(selected >= 0 ? states.at(entries.at(selected))->getState() : "") | border,

                      }
                    )
                  }
                )
            })
         });
  });

  // auto renderer2 = Renderer(radiobox2, [&] {
  //   return vbox({
  //             hbox(radiobox2->Render() | vscroll_indicator | frame | border),
  //           });
  // });

  auto topRenderer = Container::Horizontal({
    renderer // , renderer2
  });
 
  // auto screen = ScreenInteractive::FitComponent();
  screen.Loop(topRenderer);
}

std::string GetEnv(std::string key)
{
  auto value = getenv(key.c_str());

  if (value == nullptr) {
    throw std::runtime_error("environment variable "+key+" not set, exiting");
  }

  return value;
}

int main(void) // int /* argc */, char* /* argv[] */*)
{

  int msgid=1;

  curl_global_init(CURL_GLOBAL_ALL);
  auto wc = WSConn(GetEnv("HA_WS_URL"));

  auto welcome = wc.recv();

  auto jwelcome = json::parse(welcome);

  // cerr<<"got welcome: "<<welcome<<endl; // FIXME check that it is the expected auth_required message

  json auth;

  auth["type"] = "auth";
  auth["access_token"] = GetEnv("HA_API_TOKEN");

  // cerr<<auth.dump()<<endl;

  auto jauth = auth.dump();
  // cerr<<jauth<<endl;
  wc.send(jauth);

  // cerr<<wc.recv()<<endl; // FIXME assert auth_ok

  json subscribe;

  subscribe["id"] = msgid++;
  subscribe["type"] = "subscribe_events";

  auto jsubscribe = subscribe.dump();

  wc.send(jsubscribe);


  json call;

  call["id"]=msgid++;
  call["type"]="call_service";
  call["domain"]="light";
  call["service"]="toggle";
  call["target"]["entity_id"]="light.plafondlamp_kantoor_peter_level_light_color_on_off";

  auto jcall = call.dump();

  wc.send(jcall);

  json getstates;

  getstates["id"]=msgid++;
  getstates["type"]="get_states";

  auto jgetstates = getstates.dump();

  wc.send(jgetstates);

/* example ID_SUBSCRIPTION message:
{
  "id": 1,
  "type": "event",
  "event": {
    "event_type": "state_changed",
    "data": {
      "entity_id": "sensor.shellyplug_s_bc6aa8_power",
      "old_state": {
...
      },
      "new_state": {
        "entity_id": "sensor.shellyplug_s_bc6aa8_power",
        "state": "9.89",
...
 */

/* example ID_GETSTATES message:
{
  "id": 2,
  "type": "result",
  "success": true,
  "result": [
    {
      "entity_id": "light.plafondlamp_kantoor_peter_level_light_color_on_off",
      "state": "on",
      "attributes": {
        "min_color_temp_kelvin": 2000,
        "max_color_temp_kelvin": 6535,

*/

  std::thread ui(uithread);

  while (true) {
    auto msg = wc.recv();

    // cout<<msg<<endl;
    json j = json::parse(msg);


    {
      std::scoped_lock lk(stateslock);

      if (j["id"] == getstates["id"]) {
        for (auto evd : j["result"]) {
          // cerr<<evd.dump()<<endl;
          auto entity_id = evd["entity_id"];

          auto old_state = evd["old_state"];
          auto new_state = evd["new_state"];

          // cout << "entity_id=" << entity_id << ", ";
          // cout << "state=" << evd["state"];
          // cout << endl;

          states[entity_id] = std::make_shared<HAEntity>(evd);
        }
        // exit(1);
      }
      else if (j["type"] == "event") {
        auto event = j["event"];
        auto event_type = event["event_type"];
        auto evd = event["data"];
        auto entity_id = evd["entity_id"];

        auto old_state = evd["old_state"];
        auto new_state = evd["new_state"];

        // cout << "event_type=" << event_type << ", ";
        // cout << "entity_id=" << entity_id << ", ";
        // cout << "state=" << new_state["state"];
        // cout << endl;

        if (event_type == "state_changed") {
          states[entity_id] = std::make_shared<HAEntity>(new_state);
        }
      }
      else {
        // not a message we were expecting
        continue;
      }

    }
    // cerr<<"\033[2Jhave "<<states.size()<< " states" << endl;
    // cerr<<"selected = "<<selected<<endl;
    // cerr<<endl;
    // for (auto &[k,v] : states) {
    //   cout<<k<<"="<<v->getState()<<endl;
    // }
    { 
      std::scoped_lock lk(entrieslock);
      entries.clear();

      for (auto &[k,v] : states) {
        entries.push_back(k); // +":"+v->getState());
      }
    }

    screen.PostEvent(ftxui::Event::Custom);
  }
  return 0;
}
