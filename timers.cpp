#include "timers.h"

void TimersResponder::reply(std::ostream& out, cxxtools::http::Request& request, cxxtools::http::Reply& reply)
{
  if ( request.method() == "GET" ) {
     showTimers(out, request, reply);
  } else if ( request.method() == "DELETE" ) {
     deleteTimer(out, request, reply);
  } else if ( request.method() == "POST" || request.method() == "PUT" ) {
     createOrUpdateTimer(out, request, reply);
  } else {
    reply.httpReturn(501, "Only GET, DELETE, POST and PUT methods are supported.");
  }
}

void TimersResponder::createOrUpdateTimer(std::ostream& out, cxxtools::http::Request& request, cxxtools::http::Reply& reply)
{
  if ( Timers.BeingEdited() ) {
     reply.httpReturn(502, "Timers are being edited - try again later");
     return;
  }

  HtmlRequestParser p(request);

  int error = false;
  std::string error_values = "";
  static TimerValues v;

  int flags = v.ConvertFlags(p.getValueAsString("flags"));
  std::string aux = v.ConvertAux(p.getValueAsString("aux"));
  std::string file = v.ConvertFile(p.getValueAsString("file"));
  int lifetime = v.ConvertLifetime(p.getValueAsString("lifetime"));
  int priority = v.ConvertPriority(p.getValueAsString("priority"));
  int stop = v.ConvertStop(p.getValueAsString("stop"));
  int start = v.ConvertStart(p.getValueAsString("start"));
  std::string weekdays = p.getValueAsString("weekdays");
  std::string day = p.getValueAsString("day");
  cChannel* chan = v.ConvertChannel(p.getValueAsString("channel"));
  std::string event_id = p.getValueAsString("event_id");
  cEvent* event = v.ConvertEvent(event_id, chan);
  cTimer* timer_orig = v.ConvertTimer(p.getValueAsString("timer_id"));
  
  if ( timer_orig == NULL ) { //create
     if ( !v.IsFlagsValid(flags) ) { error = true; error_values += "flags, "; }
     if ( event_id.length() > 0 && event == NULL ) { error = true; error_values += "event_id, "; }
     if ( !v.IsFileValid(file) ) { error = true; error_values += "file, "; }
     if ( !v.IsLifetimeValid(lifetime) ) { lifetime = 50; }
     if ( !v.IsPriorityValid(priority) ) { priority = 99; }
     if ( !v.IsStopValid(stop) ) { error = true; error_values += "stop, "; }
     if ( !v.IsStartValid(start) ) { error = true; error_values += "start, "; }
     if ( !v.IsWeekdaysValid(weekdays) ) { error = true; error_values += "weekdays, "; }
     if ( !v.IsDayValid(day) ) { error = true; error_values += "day, "; }
     if ( chan == NULL ) { error = true; error_values += "channel, "; }
  } else { //update
     if ( !v.IsFlagsValid(flags) ) { flags = 1; }
     if ( !v.IsFileValid(file) ) { file = (std::string)timer_orig->File(); }
     if ( !v.IsLifetimeValid(lifetime) ) { lifetime = timer_orig->Lifetime(); }
     if ( !v.IsPriorityValid(priority) ) { priority = timer_orig->Priority(); }
     if ( !v.IsStopValid(stop) ) { stop = timer_orig->Stop(); }
     if ( !v.IsStartValid(start) ) { start = timer_orig->Start(); }
     if ( !v.IsWeekdaysValid(weekdays) ) { weekdays = v.ConvertWeekdays(timer_orig->WeekDays()); }
     if ( !v.IsDayValid(day) ) { day = v.ConvertDay(timer_orig->Day()); }
     if ( chan == NULL ) { chan = (cChannel*)timer_orig->Channel(); }
  }

  if (error) {
     std::string error_message = (std::string)"The following parameters aren't valid: " + error_values.substr(0, error_values.length()-2) + (std::string)"!";
     reply.httpReturn(403, error_message);
     return;
  }
 
  std::ostringstream builder;
  builder << flags << ":"
          << (const char*)chan->GetChannelID().ToString() << ":"
	  << ( weekdays != "-------" ? weekdays : "" )
          << ( weekdays == "-------" || day.empty() ? "" : "@" ) << day << ":"
          << start << ":"
          << stop << ":"
          << priority << ":"
          << lifetime << ":"
          << file << ":" 
          << aux;

  dsyslog("restfulapi: /%s/ ", builder.str().c_str());
  chan = NULL;
  if ( timer_orig == NULL ) { // create timer
     cTimer* timer = new cTimer();
     if ( timer->Parse(builder.str().c_str()) ) { 
        cTimer* checkTimer = Timers.GetTimer(timer);
        if ( checkTimer != NULL ) {
           delete timer;
           reply.httpReturn(403, "Timer already defined!"); 
           esyslog("restfulapi: Timer already defined!");
        }
        if ( event != NULL ) timer->SetEvent(event);
        Timers.Add(timer);
        Timers.SetModified();
        esyslog("restfulapi: timer created!");
     } else {
        esyslog("restfulapi: timer creation failed!");
     }
  } else {
     if ( timer_orig->Parse(builder.str().c_str()) ) {
        if ( event != NULL ) timer_orig->SetEvent(event);
        Timers.SetModified();
        esyslog("restfulapi: updating timer successful!");
     } else { 
        reply.httpReturn(403, "updating timer failed!");
        esyslog("restfulapi: updating timer failed!");
     }
  }
}

void TimersResponder::deleteTimer(std::ostream& out, cxxtools::http::Request& request, cxxtools::http::Reply& reply)
{
  if ( Timers.BeingEdited() ) {
     reply.httpReturn(502, "Timers are being edited - try again later");
     return;
  }

  QueryHandler q("/timers", request);

  int timer_number = q.getParamAsInt(0);
  timer_number--; //first timer ist 0 and not 1

  int timer_count = Timers.Count();

  if ( timer_number < 0 || timer_number >= timer_count) {
     reply.httpReturn(404, "Timer number invalid!");
  } else {
     cTimer* timer = Timers.Get(timer_number);
     if ( timer ) {
        if ( !Timers.BeingEdited())
        {
           if ( timer->Recording() ) {
              timer->Skip();
              cRecordControls::Process(time(NULL));
           }
           Timers.Del(timer);
           Timers.SetModified();
        }
     }
  }
}

void TimersResponder::showTimers(std::ostream& out, cxxtools::http::Request& request, cxxtools::http::Reply& reply)
{
  QueryHandler q("/timers", request);
  TimerList* timerList;
 
  Timers.SetModified();

  if ( q.isFormat(".json") ) {
     reply.addHeader("Content-Type", "application/json; charset=utf-8");
     timerList = (TimerList*)new JsonTimerList(&out);
  } else if ( q.isFormat(".html") ) {
     reply.addHeader("Content-Type", "text/html; charset=utf-8");
     timerList = (TimerList*)new HtmlTimerList(&out);
  } else if ( q.isFormat(".xml") ) {
     reply.addHeader("Content-Type", "text/xml; charset=utf-8");
     timerList = (TimerList*)new XmlTimerList(&out);
  } else {
     reply.httpReturn(404, "Resources are not available for the selected format. (Use: .json, .html or .xml)");
     return;
  }

  timerList->init();

  int timer_count = Timers.Count();
  cTimer *timer;
  for (int i=0;i<timer_count;i++)
  {
     timer = Timers.Get(i);
     timerList->addTimer(timer);   
  }

  timerList->finish();
  delete timerList;   
}

void operator<<= (cxxtools::SerializationInfo& si, const SerTimer& t)
{
  si.addMember("start") <<= t.Start;
  si.addMember("stop") <<= t.Stop;
  si.addMember("priority") <<= t.Priority;
  si.addMember("lifetime") <<= t.Lifetime;
  si.addMember("event_id") <<= t.EventID;
  si.addMember("weekdays") <<= t.WeekDays;
  si.addMember("day") <<= t.Day;
  si.addMember("channel") <<= t.Channel;
  si.addMember("filename") <<= t.FileName;
  si.addMember("channelname") <<= t.ChannelName;
  si.addMember("is_pending") <<= t.IsPending;
  si.addMember("is_recording") <<= t.IsRecording;
  si.addMember("is_active") <<= t.IsActive;
}

void operator>>= (const cxxtools::SerializationInfo& si, SerTimer& t)
{
  si.getMember("start") >>= t.Start;
  si.getMember("stop") >>= t.Stop;
  si.getMember("priority") >>= t.Priority;
  si.getMember("lifetime") >>= t.Lifetime;
  si.getMember("event_id") >>= t.EventID;
  si.getMember("weekdays") >>= t.WeekDays;
  si.getMember("day") >>= t.Day;
  si.getMember("channel") >>= t.Channel;
  si.getMember("filename") >>= t.FileName;
  si.getMember("channel_name") >>= t.ChannelName;
  si.getMember("is_pending") >>= t.IsPending;
  si.getMember("is_recording") >>= t.IsRecording;
  si.getMember("is_active") >>= t.IsActive;
}

void operator<<= (cxxtools::SerializationInfo& si, const SerTimers& t)
{
  si.addMember("rows") <<= t.timer;
}

TimerList::TimerList(std::ostream *out)
{
  s = new StreamExtension(out);
}

TimerList::~TimerList()
{
  delete s;
}

void HtmlTimerList::init()
{
  s->writeHtmlHeader(); 
  s->write("<ul>");
}

void HtmlTimerList::addTimer(cTimer* timer)
{
  s->write("<li>");
  s->write((char*)timer->File()); //TODO: add date, time and duration
  s->write("\n");
}

void HtmlTimerList::finish()
{
  s->write("</ul>");
  s->write("</body></html>");
}

void JsonTimerList::addTimer(cTimer* timer)
{
  SerTimer serTimer;
  serTimer.Start = timer->Start();
  serTimer.Stop = timer->Stop();
  serTimer.Priority = timer->Priority();
  serTimer.Lifetime = timer->Lifetime();
  serTimer.EventID = timer->Event() != NULL ? timer->Event()->EventID() : -1;
  serTimer.WeekDays = timer->WeekDays();
  serTimer.Day = timer->Day();
  serTimer.Channel = timer->Channel()->Number();
  serTimer.IsRecording = timer->Recording();
  serTimer.IsPending = timer->Pending();
  serTimer.FileName = StringExtension::UTF8Decode(timer->File());
  serTimer.ChannelName = StringExtension::UTF8Decode(timer->Channel()->Name());
  serTimer.IsActive = timer->Flags() & 0x01 == 0x01 ? true : false;
  serTimers.push_back(serTimer);
}

void JsonTimerList::finish()
{
  cxxtools::JsonSerializer serializer(*s->getBasicStream());
  serializer.serialize(serTimers, "timers");
  serializer.finish();
}

void XmlTimerList::init()
{
  s->writeXmlHeader();
  s->write("<timers xmlns=\"http://www.domain.org/restfulapi/2011/timers-xml\">\n");
}

void XmlTimerList::addTimer(cTimer* timer)
{
  s->write(" <timer>\n");
  s->write((const char*)cString::sprintf("  <param name=\"start\">%i</param>\n", timer->Start()) );
  s->write((const char*)cString::sprintf("  <param name=\"stop\">%i</param>\n", timer->Stop()) );
  s->write((const char*)cString::sprintf("  <param name=\"priority\">%i</param>\n", timer->Priority()) );
  s->write((const char*)cString::sprintf("  <param name=\"lifetime\">%i</param>\n", timer->Lifetime()) );
  s->write((const char*)cString::sprintf("  <param name=\"event_id\">%i</param>\n", timer->Event() != NULL ? timer->Event()->EventID() : -1) );
  s->write((const char*)cString::sprintf("  <param name=\"weekdays\">%i</param>\n", timer->WeekDays()) );
  s->write((const char*)cString::sprintf("  <param name=\"day\">%i</param>\n", (int)timer->Day()));
  s->write((const char*)cString::sprintf("  <param name=\"channel\">%i</param>\n", timer->Channel()->Number()) );
  s->write((const char*)cString::sprintf("  <param name=\"is_recording\">%s</param>\n", timer->Recording() ? "true" : "false" ) );
  s->write((const char*)cString::sprintf("  <param name=\"is_pending\">%s</param>\n", timer->Pending() ? "true" : "false" ));
  s->write((const char*)cString::sprintf("  <param name=\"filename\">%s</param>\n", StringExtension::encodeToXml(timer->File()).c_str()) );
  s->write((const char*)cString::sprintf("  <param name=\"channelname\">%s</param>\n", StringExtension::encodeToXml(timer->Channel()->Name()).c_str()));
  s->write((const char*)cString::sprintf("  <param name=\"is_active\">%s</param>\n", timer->Flags() & 0x01 == 0x01 ? "true" : "false" ));
  s->write(" </timer>\n");
}

void XmlTimerList::finish()
{
  s->write("</timers>");
}

// --- TimerValues class ------------------------------------------------------------

std::stack<int> TimerValues::ConvertToBinary(int v)
{
   int b;
   std::stack <int> res;

   while ( v != 0) {
     b = v % 2;
     res.push(b);
     v = (v-b) / 2;
   }
   return res;
}

bool TimerValues::IsDayValid(std::string v)
{
  static cxxtools::Regex regex("[0-9]{4,4}-[0-9]{1,2}-[0-9]{1,2}");
  return regex.match(v);
}

bool TimerValues::IsFlagsValid(int v)
{
  if ( v == 0x0000 || v == 0x0001 || v == 0x0002 || v == 0x0004 || v == 0x0008 || v == 0xFFFF ) 
     return true;
  return false;
}

bool TimerValues::IsFileValid(std::string v) 
{
  if ( v.length() > 0 && v.length() <= 40 ) 
     return true;
  return false;
}

bool TimerValues::IsLifetimeValid(int v) 
{
  if ( v >= 0 && v <= 99 )
     return true;
  return false;
}

bool TimerValues::IsPriorityValid(int v)
{
  return IsLifetimeValid(v); //uses the same values as the lifetime
}

bool TimerValues::IsStopValid(int v)
{
  int minutes = v % 100;
  int hours = (v - minutes) / 100;
  if ( minutes >= 0 && minutes < 60 && hours >= 0 && hours < 24 )
     return true;
  return false;

}

bool TimerValues::IsStartValid(int v)
{
  return IsStopValid(v); //uses the syntax as start time, f.e. 2230 means half past ten in the evening
}

bool TimerValues::IsWeekdaysValid(std::string v)
{
  /*static cxxtools::Regex regex("[\\-M][\\-T][\\-W][\\-T][\\-F][\\-S][\\-S]");
  return regex.match(v);*/
  if ( v.length() != 7 ) return false;
  const char* va = v.c_str();
  if ( va[0] != '-' && va[0] != 'M' ) return false;
  if ( va[1] != '-' && va[1] != 'T' ) return false;
  if ( va[2] != '-' && va[2] != 'W' ) return false;
  if ( va[3] != '-' && va[3] != 'T' ) return false;
  if ( va[4] != '-' && va[4] != 'F' ) return false;
  if ( va[5] != '-' && va[5] != 'S' ) return false;
  if ( va[6] != '-' && va[6] != 'S' ) return false;
  return true;
}

int TimerValues::ConvertFlags(std::string v)
{
  return StringExtension::strtoi(v);
}

cEvent* TimerValues::ConvertEvent(std::string event_id, cChannel* channel)
{
  if ( channel == NULL ) return NULL;

  int eventid = StringExtension::strtoi(event_id);
  if ( eventid <= -1 ) return NULL;

  cSchedulesLock MutexLock;
  const cSchedules *Schedules = cSchedules::Schedules(MutexLock);
  if ( !Schedules ) return NULL;

  const cSchedule *Schedule = Schedules->GetSchedule(channel->GetChannelID());

  if ( !Schedule ) return NULL;

  return (cEvent*)Schedule->GetEvent(eventid);
}

std::string TimerValues::ConvertFile(std::string v)
{
  return StringExtension::replace(v, ":", "|");
}

std::string TimerValues::ConvertAux(std::string v)
{
  return ConvertFile(v);
}

int TimerValues::ConvertLifetime(std::string v)
{
  return StringExtension::strtoi(v);
}

int TimerValues::ConvertPriority(std::string v)
{
  return StringExtension::strtoi(v);
}

int TimerValues::ConvertStop(std::string v)
{
  return StringExtension::strtoi(v);
}

int TimerValues::ConvertStart(std::string v)
{
  return StringExtension::strtoi(v);
}

int TimerValues::ConvertDay(std::string v)
{
  return StringExtension::strtoi(v);
}

std::string TimerValues::ConvertDay(time_t v)
{
  struct tm *timeinfo = localtime(&v);
  std::ostringstream str;
  str << timeinfo->tm_year << "-" << (timeinfo->tm_mon + 1) << "-" << timeinfo->tm_mday;
  delete timeinfo;
  return str.str();;
}

cChannel* TimerValues::ConvertChannel(std::string v)
{
  int c = StringExtension::strtoi(v);
  return VdrExtension::getChannel(c);
}

cTimer* TimerValues::ConvertTimer(std::string v)
{
  int t = StringExtension::strtoi(v);
  if ( t >= 0 ) {
     return Timers.Get(t);
  }
  return NULL;
}

std::string TimerValues::ConvertWeekdays(int v)
{
  std::stack<int> b = ConvertToBinary(v);
  int counter = 0;
  std::ostringstream res;
  while ( !b.empty() && counter < 7 ) {
     int val = b.top();
     switch(counter) {
       case 0: res << (val == 1 ? 'M' : '-'); break;
       case 1: res << (val == 1 ? 'T' : '-'); break;
       case 2: res << (val == 1 ? 'W' : '-'); break;
       case 3: res << (val == 1 ? 'T' : '-'); break;
       case 4: res << (val == 1 ? 'F' : '-'); break;
       case 5: res << (val == 1 ? 'S' : '-'); break;
       case 6: res << (val == 1 ? 'S' : '-'); break;
     }
     b.pop();
     counter++;
  }
  return res.str();
}

int TimerValues::ConvertWeekdays(std::string v)
{
 const char* str = v.c_str();
 int res = 0;
 if ( str[0] == 'M' ) res += 64;
 if ( str[1] == 'T' ) res += 32;
 if ( str[2] == 'W' ) res += 16;
 if ( str[3] == 'T' ) res += 8;
 if ( str[4] == 'F' ) res += 4;
 if ( str[5] == 'S' ) res += 2;
 if ( str[6] == 'S' ) res += 1;
 return res;
}
