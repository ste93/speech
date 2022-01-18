/*
 * Copyright (C) 2018 iCub Facility - Istituto Italiano di Tecnologia
 * Author: Ilaria Carlini Laura Cavaliere Vadim Tikhanoff 
 * email:  ilaria.carlini@iit.it laura.cavaliere@iit.it vadim.tikhanoff@iit.it 
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
 */

#include <vector>
#include <iostream>
#include <deque>
#include <cstdio>
#include <cmath>

#include <fstream>
#include <iterator>
#include <string>
#include <map>

#include <yarp/os/BufferedPort.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/os/RFModule.h>
#include <yarp/os/Network.h>
#include <yarp/os/Time.h>
#include <yarp/os/Log.h>
#include <yarp/os/LogStream.h>
#include <yarp/os/Semaphore.h>
#include <yarp/sig/SoundFile.h>
#include <yarp/dev/PolyDriver.h>

#include <grpc++/grpc++.h>
#include "google/cloud/language/v1/language_service.grpc.pb.h"
#include "google/cloud/texttospeech/v1/cloud_tts.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/agent.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/audio_config.grpc.pb.h"
//#include "google/cloud/dialogflow/cx/v3beta1/context.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/entity_type.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/intent.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/session_entity_type.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/session.grpc.pb.h"
#include "google/cloud/dialogflow/cx/v3beta1/webhook.grpc.pb.h"

#include <chrono>
#include <ctime>
#include <algorithm>

#include "googleDialog_IDL.h"

using namespace google::cloud::language::v1;
using namespace google::cloud::texttospeech::v1;
using namespace google::cloud::dialogflow::cx::v3beta1;
bool is_changed;

static const std::map<grpc::StatusCode, std::string> status_code_to_string {
    {grpc::OK, "ok"},
    {grpc::CANCELLED, "cancelled"},
    {grpc::UNKNOWN, "unknown"},
    {grpc::INVALID_ARGUMENT, "invalid_argument"},
    {grpc::DEADLINE_EXCEEDED, "deadline_exceeded"},
    {grpc::NOT_FOUND, "not_found"},
    {grpc::ALREADY_EXISTS, "already_exists"},
    {grpc::PERMISSION_DENIED, "permission_denied"},
    {grpc::UNAUTHENTICATED, "unauthenticated"},
    {grpc::RESOURCE_EXHAUSTED , "resource_exhausted"},
    {grpc::FAILED_PRECONDITION, "failed_precondition"},
    {grpc::ABORTED, "aborted"},
    {grpc::OUT_OF_RANGE, "out_of_range"},
    {grpc::UNIMPLEMENTED, "unimplemented"},
    {grpc::INTERNAL, "internal"},
    {grpc::UNAVAILABLE, "unavailable"},
    {grpc::DATA_LOSS, "data_loss"},
    {grpc::DO_NOT_USE, "do_not_use"}
};
/********************************************************/
class Processing : public yarp::os::BufferedPort<yarp::os::Bottle>
{
    std::string moduleName;  
    std::string session_id;  
    std::string agent_name;  
    std::string language_code; 
    std::string &state; 
    bool &input_is_empty, reset; 
    std::int64_t &processing_time; 
    yarp::os::RpcServer handlerPort;
    yarp::os::BufferedPort<yarp::os::Bottle> targetPort;

public:
    /********************************************************/

    Processing( const std::string &moduleName, const std::string agent_name, const std::string language_code, std::string &state, bool &input_is_empty, std::int64_t &processing_time)  : state(state), input_is_empty(input_is_empty), processing_time(processing_time)
    {
        this->moduleName = moduleName;
        this->session_id = getRandSession();
        this->agent_name = agent_name;
        this->language_code = language_code;
        reset = false;
        yInfo()<< "State: " << state;
        yInfo()<< "input_is_empty: " << input_is_empty;
    }

    /********************************************************/
    ~Processing()
    {

    };
    /********************************************************/
    std::string getRandSession() {
        std::string session;
        srand(time(NULL));
        for(int i = 0; i<16; i++) {
            session += std::to_string(rand() % 10);
        }
        return session;
    }
    /********************************************************/
    bool open()
    {   
        this->useCallback();
        yarp::os::BufferedPort<yarp::os::Bottle >::open( "/" + moduleName + "/text:i" );
        targetPort.open("/"+ moduleName + "/result:o");

        return true;
    }

    /********************************************************/
    void close()
    {
        yarp::os::BufferedPort<yarp::os::Bottle >::close();
        targetPort.close();
    }

    /********************************************************/
    void onRead( yarp::os::Bottle &bot)
    {   
        yarp::os::Bottle &outTargets = targetPort.prepare();

        outTargets.clear();
        outTargets = queryGoogleDialog(bot);
        yDebug() << "bottle" << outTargets.toString();
        if(outTargets.size()>0){
           targetPort.write();
        }
        yInfo() << "State: " << state;
        yDebug() << "done querying google";
    }

/********************************************************/
    yarp::os::Bottle queryGoogleDialog(yarp::os::Bottle& bottle)
    {
        std::string text = bottle.toString();
        yarp::os::Bottle result;
        //google::cloud::dialogflow::cx::v3beta1::TextInput text_input;
        //google::cloud::dialogflow::cx::v3beta1::QueryInput query_input;

        //std::string language_code = "en-English";
        //text_input.set_text(text.c_str());
        //query_input.set_allocated_text(&text_input);
        //query_input.set_language_code(language_code);

        grpc::Status status;
        grpc::ClientContext context;

        ListIntentsRequest request;
        ListIntentsResponse response;
        std::string parent = "projects/team-code-dev/locations/global/agents/fbfc6fd6-065b-4415-a044-0a7190480b89";
        request.set_parent(parent);
        /*request.set_session(agent_name+"/environments/draft/sessions/"+session_id);
        request.set_allocated_query_input(&query_input);
        std::string user_express = request.query_input().text().text();
        yInfo() << "End-user expression:" << user_express;*/
        if(request.parent() != ""){
            input_is_empty=false;
            auto creds = grpc::GoogleDefaultCredentials();
            auto channel = grpc::CreateChannel("dialogflow.googleapis.com", creds);
            std::unique_ptr<Intents::Stub> dialog (Intents::NewStub(channel));
            yarp::os::Time::delay(0.2);
            const std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();

            grpc::Status dialog_status = dialog->ListIntents(&context, request, &response);
            std::string status_string = status_code_to_string.at(dialog_status.error_code());
            result.clear();
            if ( dialog_status.ok() ) {
                const auto end = std::chrono::steady_clock::now();
                processing_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                yDebug() << "Request processing time: " << processing_time << "µs";  

                yInfo() << "Status returned OK";
                yInfo() << "\n------Response------\n";
                // if (response.intents().size() > 0) 
                // {
                    //result.addString(response.query_result().response_messages().Get(0).text().text().Get(0).c_str());
                    for (auto intent_local: response.intents()) {
                    yError() << "intent" << intent_local.display_name().c_str();

                    }
                // }
                // else if (reset)
                // {
                //     reset=false;
                // }
            } 
            else if ( !dialog_status.ok() ) {
                yError() << "Status Returned Cancelled";
            }	      
        }
        return result;
    }

   /********************************************************/
    bool setLanguageCode(const std::string &languageCode)
    {
        language_code = languageCode;
        return true;
    }

    /********************************************************/
    std::string getLanguageCode()
    {
        return language_code;
    }

    /********************************************************/
    bool start_acquisition()
    {
        return true;
    }

    /********************************************************/
    bool stop_acquisition()
    {
        return true;
    }

    /********************************************************/
    bool checkState(std::string new_state)
    {   
        if(new_state!=state){
            is_changed=true;
            state=new_state;
        }
        else{
            is_changed=false;
        }
        return is_changed;
    }
    
    /********************************************************/
    void resetDialog(){
         reset = true;
         yarp::os::Bottle bot;
         bot.clear();
         bot.addString("Exit");
	 queryGoogleDialog(bot);
    }
};

/********************************************************/
class Module : public yarp::os::RFModule, public googleDialog_IDL
{
    yarp::os::ResourceFinder    *rf;
    yarp::os::RpcServer         rpcPort;
    std::string state;
    std::int64_t processing_time;
    bool input_is_empty;
    yarp::os::BufferedPort<yarp::os::Bottle> statePort;

    Processing                  *processing;
    friend class                processing;

    bool                        closing;
    std::vector<std::string>    allLanguageCodes;

    /********************************************************/
    bool attach(yarp::os::RpcServer &source)
    {
        return this->yarp().attachAsServer(source);
    }

public:

    /********************************************************/
    bool configure(yarp::os::ResourceFinder &rf)
    {
        this->rf=&rf;
        this->state="Ready";
        this->input_is_empty=false;
        this->processing_time=0;
        std::string moduleName = rf.check("name", yarp::os::Value("googleDialog"), "module name (string)").asString();
        std::string agent_name = rf.check("agent", yarp::os::Value("1"), "name of the agent").asString();
        std::string language_code = rf.check("language", yarp::os::Value("en-US"), "language of the dialogflow").asString();
        
        if (rf.check("languageCodes", "Getting language codes"))
        {
            yarp::os::Bottle &grp=rf.findGroup("languageCodes");
            int sz=grp.size()-1;

            for (int i=0; i<sz; i++)
                allLanguageCodes.push_back(grp.get(1+i).asString());
        }

        yDebug() << "this is the project" << rf.check("project");
        yDebug() << "Module name" << moduleName;
        yDebug() << "agent name" << agent_name;
        yDebug() << "language of the dialog" << language_code;
        setName(moduleName.c_str());

        rpcPort.open(("/"+getName("/rpc")).c_str());
        statePort.open("/"+ moduleName + "/state:o");

        closing = false;

        processing = new Processing( moduleName, agent_name, language_code, state, input_is_empty, processing_time);

        /* now start the thread to do the work */
        processing->open();

        attach(rpcPort);

        return true;
    }

    /********************************************************/
    bool setLanguage(const std::string& languageCode)
    {
        bool returnVal = false;
        
        std::string language;
        
        for (int i = 0; i < allLanguageCodes.size(); i++)
        {
            if (languageCode == allLanguageCodes[i])
            {
                language = languageCode;
                processing->setLanguageCode(languageCode);
                returnVal = true;
                break;
            }
        }

        return returnVal;
    }

    /********************************************************/
    std::string getLanguageCode()
    {
        return processing->getLanguageCode();
    }

    /**********************************************************/
    bool close()
    {   
        statePort.close();
        processing->close();
        delete processing;
        return true;
    }

    /********************************************************/
    double getPeriod()
    {
        return 0.1;
    }

    /********************************************************/
    bool quit()
    {
        closing=true;
        return true;
    }

    /********************************************************/
    bool updateModule()
    {   
        if(is_changed){
            is_changed=false;
            yarp::os::Bottle &outTargets = statePort.prepare();   
            outTargets.clear();  
            outTargets.addString(state);
            yDebug() << "outTarget:" << outTargets.toString().c_str();
            statePort.write();
        }  
        return !closing;
    }
    
    /********************************************************/
    std::string getState()
    {  
        return state;
    }
 
    /********************************************************/
    std::int64_t getProcessingTime()
    {  
        return processing_time;
    }

    /********************************************************/
    bool resetDialog()
    { 
	processing->resetDialog();
        return true;
    }
};

/********************************************************/
int main(int argc, char *argv[])
{
    yarp::os::Network::init();
    yInfo() << "starting";

    yarp::os::Network yarp;
    yInfo() << __LINE__;

    if (!yarp.checkNetwork())
    {
        yError("YARP server not available!");
        return 1;
    }
    yInfo() << __LINE__;


    Module module;
    yarp::os::ResourceFinder rf;

    rf.setVerbose( true );
    rf.setDefaultContext( "googleDialog" );
        yInfo() << __LINE__;

    rf.setDefaultConfigFile( "config.ini" );
    yInfo() << __LINE__;
    rf.setDefault("name","googleDialog");
    yInfo() << __LINE__;
    rf.configure(argc,argv);
    yInfo() << "before configure" << __LINE__;

    return module.runModule(rf);
}
