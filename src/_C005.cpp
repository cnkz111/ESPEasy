#include "src/Helpers/_CPlugin_Helper.h"
#ifdef USES_C005

# include "src/Commands/InternalCommands.h"
# include "src/Globals/EventQueue.h"
# include "src/Globals/ExtraTaskSettings.h"
# include "src/Helpers/PeriodicalActions.h"
# include "src/Helpers/StringParser.h"
# include "_Plugin_Helper.h"

// #######################################################################################################
// ################### Controller Plugin 005: Home Assistant (openHAB) MQTT ##############################
// #######################################################################################################

# define CPLUGIN_005
# define CPLUGIN_ID_005         5
# define CPLUGIN_NAME_005       "Home Assistant (openHAB) MQTT"

String CPlugin_005_pubname;
bool   CPlugin_005_mqtt_retainFlag = false;

bool C005_parse_command(struct EventStruct *event);

bool CPlugin_005(CPlugin::Function function, struct EventStruct *event, String& string)
{
  bool success = false;

  switch (function)
  {
    case CPlugin::Function::CPLUGIN_PROTOCOL_ADD:
    {
      Protocol[++protocolCount].Number     = CPLUGIN_ID_005;
      Protocol[protocolCount].usesMQTT     = true;
      Protocol[protocolCount].usesTemplate = true;
      Protocol[protocolCount].usesAccount  = true;
      Protocol[protocolCount].usesPassword = true;
      Protocol[protocolCount].usesExtCreds = true;
      Protocol[protocolCount].defaultPort  = 1883;
      Protocol[protocolCount].usesID       = false;
      break;
    }

    case CPlugin::Function::CPLUGIN_GET_DEVICENAME:
    {
      string = F(CPLUGIN_NAME_005);
      break;
    }

    case CPlugin::Function::CPLUGIN_INIT:
    {
      success = init_mqtt_delay_queue(event->ControllerIndex, CPlugin_005_pubname, CPlugin_005_mqtt_retainFlag);
      break;
    }

    case CPlugin::Function::CPLUGIN_EXIT:
    {
      exit_mqtt_delay_queue();
      break;
    }

    case CPlugin::Function::CPLUGIN_PROTOCOL_TEMPLATE:
    {
      event->String1 = F("%sysname%/#");
      event->String2 = F("%sysname%/%tskname%/%valname%");
      break;
    }

    case CPlugin::Function::CPLUGIN_PROTOCOL_RECV:
    {
      controllerIndex_t ControllerID = findFirstEnabledControllerWithId(CPLUGIN_ID_005);

      if (validControllerIndex(ControllerID)) {
        C005_parse_command(event);
      }
      break;
    }

    case CPlugin::Function::CPLUGIN_PROTOCOL_SEND:
    {
      String pubname         = CPlugin_005_pubname;
      bool   mqtt_retainFlag = CPlugin_005_mqtt_retainFlag;

      LoadTaskSettings(event->TaskIndex);
      parseControllerVariables(pubname, event, false);

      uint8_t valueCount = getValueCountForTask(event->TaskIndex);

      for (uint8_t x = 0; x < valueCount; x++)
      {
        // MFD: skip publishing for values with empty labels (removes unnecessary publishing of unwanted values)
        if (ExtraTaskSettings.TaskDeviceValueNames[x][0] == 0) {
          continue; // we skip values with empty labels
        }
        String tmppubname = pubname;
        parseSingleControllerVariable(tmppubname, event, x, false);
        String value;
        if (event->sensorType == Sensor_VType::SENSOR_TYPE_STRING) {
          value = event->String2.substring(0, 20); // For the log
        } else {
          value = formatUserVarNoCheck(event, x);
        }
# ifndef BUILD_NO_DEBUG

        if (loglevelActiveFor(LOG_LEVEL_DEBUG)) {
          String log = F("MQTT : ");
          log += tmppubname;
          log += ' ';
          log += value;
          addLogMove(LOG_LEVEL_DEBUG, log);
        }
# endif // ifndef BUILD_NO_DEBUG

        // Small optimization so we don't try to copy potentially large strings
        if (event->sensorType == Sensor_VType::SENSOR_TYPE_STRING) {
          MQTTpublish(event->ControllerIndex, event->TaskIndex, tmppubname.c_str(), event->String2.c_str(), mqtt_retainFlag);
        } else {
          // Publish using move operator, thus tmppubname and value are empty after this call
          MQTTpublish(event->ControllerIndex, event->TaskIndex, std::move(tmppubname), std::move(value), mqtt_retainFlag);
        }
      }
      break;
    }

    case CPlugin::Function::CPLUGIN_FLUSH:
    {
      processMQTTdelayQueue();
      delay(0);
      break;
    }

    default:
      break;
  }

  return success;
}

bool C005_parse_command(struct EventStruct *event) {
  // FIXME TD-er: Command is not parsed for template arguments.

  // Topic  : event->String1
  // Message: event->String2
  String cmd;
  bool   validTopic          = false;
  const int lastindex        = event->String1.lastIndexOf('/');
  const String lastPartTopic = event->String1.substring(lastindex + 1);
  const bool has_cmd_arg_index = event->String1.lastIndexOf(F("cmd_arg")) != -1;

  if (lastPartTopic.equals(F("cmd"))) {
    // Example:
    // Topic: ESP_Easy/Bathroom_pir_env/cmd
    // Message: gpio,14,0
    // Full command:  gpio,14,0

    cmd = event->String2;

    // SP_C005a: string= ;cmd=gpio,12,0 ;taskIndex=12 ;string1=ESPT12/cmd ;string2=gpio,12,0
    validTopic = true;
  } else if (has_cmd_arg_index) {
    // Example:
    // Topic: ESP_Easy/Bathroom_pir_env/cmd_arg1/GPIO/0
    // Message: 14
    // Full command: gpio,14,0

    uint8_t topic_index = 1;
    String topic_folder = parseStringKeepCase(event->String1, topic_index, '/');

    while(!topic_folder.startsWith(F("cmd_arg")) && !topic_folder.isEmpty()) {
      ++topic_index;
      topic_folder = parseStringKeepCase(event->String1, topic_index, '/');
    }
    if (!topic_folder.isEmpty()) {
      int cmd_arg_nr = -1;
      if (validIntFromString(topic_folder.substring(7), cmd_arg_nr)) {
        int constructed_cmd_arg_nr = 0;
        ++topic_index;
        topic_folder = parseStringKeepCase(event->String1, topic_index, '/');
        bool msg_added = false;
        while(!topic_folder.isEmpty()) {
          if (constructed_cmd_arg_nr != 0) {
            cmd += ',';
          }
          if (constructed_cmd_arg_nr == cmd_arg_nr) {
            cmd += event->String2;
            msg_added = true;
          } else {
            cmd += topic_folder;
            ++topic_index;
            topic_folder = parseStringKeepCase(event->String1, topic_index, '/');
          }
          ++constructed_cmd_arg_nr;
        }
        if (!msg_added) {
          cmd += ',';
          cmd += event->String2;
        }
        //addLog(LOG_LEVEL_INFO, String(F("MQTT cmd: ")) + cmd);

        validTopic = true;
      }
    }
  } else {
    // Example:
    // Topic: ESP_Easy/Bathroom_pir_env/GPIO/14
    // Message: 0 or 1
    // Full command:  gpio,14,0
    if (lastindex > 0) {
      // Topic has at least one separator
      int   lastPartTopic_int;
      float value_f;

      if (validFloatFromString(event->String2, value_f) &&
          validIntFromString(lastPartTopic, lastPartTopic_int)) {
        int prevLastindex = event->String1.lastIndexOf('/', lastindex - 1);
        cmd        = event->String1.substring(prevLastindex + 1, lastindex);
        cmd       += ',';
        cmd       += lastPartTopic_int;
        cmd       += ',';
        cmd       += event->String2; // Just use the original format
        validTopic = true;
      }
    }
  }

  if (validTopic) {
    // in case of event, store to buffer and return...
    const String command = parseString(cmd, 1);

    if ((command.equals(F("event"))) || (command.equals(F("asyncevent")))) {
      if (Settings.UseRules) {
        eventQueue.addMove(parseStringToEnd(cmd, 2));
      }
    } else {
      ExecuteCommand_all(EventValueSource::Enum::VALUE_SOURCE_MQTT, cmd.c_str());
    }
  }
  return validTopic;
}

#endif // ifdef USES_C005
