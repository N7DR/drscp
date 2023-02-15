// $Id: drscp.h 7 2023-02-03 17:28:36Z n7dr $

// Released under the GNU Public License, version 2
//   see: https://www.gnu.org/licenses/gpl-2.0.html

// Principal author: N7DR

// Copyright owners:
//    N7DR

/*! \file   drscp.h

    Functions and classes related to SCP processing
*/

#ifndef DRSCP_H
#define DRSCP_H

#include "string_functions.h"

extern bool DISPLAY_BAD_QSOS;

enum class HF_BAND { B160 = 0,
                     B80,
                     B40,
                     B20,
                     B15,
                     B10,
                     BAD
                   };

static const std::vector<std::string> HF_BAND_STR { "160"s, "80"s, "40"s, "20"s, "15"s, "10"s, "BAD"s };

class small_qso;
class contest_parameters;

using CALL_MAP = std::map<std::string, int, decltype(&compare_calls)>;      // accumulator in callsign order
using CALL_SET = std::set<std::string, decltype(&compare_calls)>;           // set in callsign order

// forward declarations
HF_BAND                band_from_qrg(const int qrg);
auto                   build_minilog(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& qsos_per_call) -> std::unordered_map<HF_BAND, base_type<decltype(qsos_per_call)>>;
std::vector<small_qso> build_vec(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& qsos_per_call);

std::unordered_set<std::string> calls_with_unreliable_freq(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos, const std::unordered_set<std::string>& calls_with_no_freq_info);

std::pair<std::vector<small_qso>::const_iterator, std::vector<small_qso>::const_iterator> get_bounds(const int target_minutes, const int minimum_minutes, const int maximum_minutes,
                                                                                                     const int ALLOWED_SKEW, const std::vector<small_qso>& vec);
                                                                                                     
bool    is_bust(const std::string& call, const std::string& copied) noexcept;
bool    is_stn_running(const std::string& call, const int rel_mins, const int qrg, const std::unordered_set<std::string>& tcalls, const std::unordered_set<std::string>& calls_with_no_freq_info,
                       const std::unordered_set<std::string>& calls_with_poor_freq_info, const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos_this_band,
                       const std::vector<small_qso>& all_vec,
                       const std::vector<std::vector<small_qso>::const_iterator>& all_time_map, const int minimum_minutes, const int maximum_minutes,
                       const std::string& ignore_call);
                      
std::unordered_set<std::string> process_band(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& pruned_qsos_this_band,
                                             const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos_this_band,
                                             const std::unordered_set<std::string>& calls_with_no_freq_info,
                                             const std::unordered_set<std::string>& calls_with_poor_freq_info,
                                             const int max_rel_mins);
CALL_MAP process_directory(const contest_parameters& cp);

int remove_qsos_outside_contest_period(std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos);

// operators to append to CALL_MAP
void operator+=(CALL_MAP& cm, const std::unordered_set<std::string>& us)
  { std::for_each( us.cbegin(), us.cend(), [&cm] (auto& element) { (cm[element])++; } ); }

void operator+=(CALL_MAP& cm, const CALL_MAP& us)
  { std::for_each( us.cbegin(), us.cend(), [&cm] (auto& pr) { cm[pr.first] += pr.second; } ); }

void operator+=(CALL_MAP& cm, const std::string& call)
  { (cm[call])++; }
  
// -----------  small_qso  ----------------

/*! \class  small_qso
    \brief  Minimal data about a logged QSO
*/

static constinit int qso_id { 0 };      ///< global QSO counter

class small_qso         // small QSO
{
protected:
  
  std::string _tcall { };           ///< transmitted call
  std::string _rcall { };           ///< received call
  
  HF_BAND _band { HF_BAND::BAD };   ///< band
  
  int _qrg { };                     ///< frequency, in kHz
  
  time_t _time     { };             ///< UTC time
  int    _rel_mins { };             ///< relative minutes from the start of the contest
  
  int _id;                          ///< unique QSO identifier
  
public:

  small_qso(void) = default;        // provide default constructor; used only when encountering an error during construction

/*! \brief              Constructor
    \param  qso_fields  fields taken from a line in a Cabrillo file
*/
small_qso(const std::vector<std::string_view>& qso_fields) :
    _id(qso_id++)
  { auto process_error = [qso_fields, this] (const std::string& msg)
      { if (DISPLAY_BAD_QSOS)
        { std::cerr << msg << ": ";
      
          for (const auto& field : qso_fields)
            std::cerr << field << " ";
          std::cerr << std::endl;
        }
      
        *this = small_qso { };
      };
    
    if (qso_fields.size() < 9)
    { process_error("ERROR constructing small_qso from short vector");
      return;
    }
    
    _qrg = from_string<int>(std::string { qso_fields[1] });
    _tcall = qso_fields[5];
    _rcall = qso_fields[8];
    
    if (!contains_letter(_tcall))
    { process_error("tcall does not contain letter");
      return;
    }

    if (!contains_digit(_tcall))
    { process_error("tcall does not contain digit");
      return;
    }

    if (!contains_letter(_rcall))
    { process_error("rcall does not contain letter");
      return;
    }

    if (!contains_digit(_rcall))
    { process_error("rcall does not contain digit");
      return;
    }
    
    try
    { _band = band_from_qrg(_qrg);
    }
    
    catch (...)
    { process_error("error in frequency");
      return;
    }
      
// we don't yet have proper support for date/time in g++
    struct tm t { };
    
    const std::string dat { qso_fields[3] };
    const std::string utc { qso_fields[4] };

// see: https://www.epochconverter.com/programming/c
    t.tm_year = from_string<int>(substring(dat, 0, 4)) - 1900;
    t.tm_mon  = from_string<int>(substring(dat, 5, 2)) - 1;
    t.tm_mday = from_string<int>(substring(dat, 8, 2));
    t.tm_hour = from_string<int>(substring(utc, 0, 2));
    t.tm_min  = from_string<int>(substring(utc, 2, 2));
    
    _time = timegm(&t);
  }

/*! \brief              Constructor
    \param  qso_line    line from a Cabrillo file
*/
  small_qso(std::string_view qso_line)
  { const std::vector<std::string_view> qso_fields { split_string_sv(qso_line, ' ') };  // assumes has already been squashed
  
    *this = small_qso(qso_fields);
  }

  READ_AND_WRITE(tcall);
  READ_AND_WRITE(rcall);
  READ(band);
  READ(qrg);
  READ_AND_WRITE(time);
  READ(id);
  READ_AND_WRITE(rel_mins);

/// small_qso < small_qso
  inline bool operator<(const small_qso& sq) const
    { return (_time < sq._time); }
};

/// ostream << small_qso
std::ostream& operator<<(std::ostream& ost, const small_qso& sq)
{ ost << "Id: " << sq.id() << ", time = " << sq.time() << ", band = " << HF_BAND_STR.at(static_cast<int>(sq.band())) << "m"
      << ", qrg = " << sq.qrg() << ", tcall = " << sq.tcall() << ", rcall = " << sq.rcall();
  
  return ost;
}

/*! \brief                              Does a call's log have valid frequency information?
    \param  call                        target call
    \param  calls_with_no_freq_info     calls that have no reliable frequency info in the log
    \param  calls_with_poor_freq_info   entrants whose logged frequency information is untrustworthy
    \return                             whether <i>call<i/> has a log with valid frequency information
    
*/
inline bool call_has_good_freq_info(const std::string& call, const std::unordered_set<std::string>& calls_with_no_freq_info, const std::unordered_set<std::string>& calls_with_poor_freq_info)
  { return (!calls_with_no_freq_info.contains(call) and !calls_with_poor_freq_info.contains(call)); }

/*! \brief          Given a container of calls, for each one return a list of possible busts from the container
    \param  calls   container of calls
    \return         for each call in <i>calls</i> a set of possible busts of the call from those in <i>calls</i>
    
    If there are no possible busts for a call, no entry is placed into the map
*/
template <typename C>
  requires is_string<typename C::value_type>
std::unordered_map<std::string /* call */, std::unordered_set<std::string> /* possible_busts */> possible_busts(const C& calls)
{ std::unordered_map<std::string, std::unordered_set<std::string>> rv { };

  for (auto it1 { calls.cbegin() }; it1 != calls.cend(); ++it1)
  { const std::string& call1 { *it1 };

    for (auto it2 { next(it1) }; it2 != calls.cend(); ++it2)  
    { const std::string& call2 { *it2 };
    
      if (is_bust(call1, call2))
      { rv[call1] += call2;
        rv[call2] += call1;         // busting is symmetrical
      }
    }
  }
  
  return rv;
}

// -----------  contest_parameters  ----------------

/*! \class  contest_parameters
    \brief  Values associated with a contest
*/

class contest_parameters
{
protected:

  std::string  _directory { };      ///< directory that contains the logs
  int          _hours     { };      ///< duration of the contest, in hours               
  time_t       _t_start   { };      ///< time of the start of the contest
  time_t       _t_end     { };      ///< one second past the end of the contest
  
public:

/// construct from line containing three parameters
// directory YYYY-MM-DD[THH[:MM[:SS]]] hh 
  contest_parameters(const std::string& str)
  { const std::vector<std::string> fields { split_string(squash(str), ' ') };
  
    if (fields.size() != 3)
    { std::cerr << "ERROR: not three fields in line: " << str << std::endl;
      exit(-1);
    }
    
    _directory = fields[0];
    
    struct std::tm t;
    
    if (fields[1].size() < 10)
    { std::cerr << "ERROR: date/time too short: " << str << std::endl;
      exit(-1);
    }
    
    const std::string dt { fields[1] };
    
    const int year { from_string<int>(substring(dt, 0, 4)) };
    t.tm_year = year - 1900;
    
    const int mon = from_string<int>(substring(dt, 5, 2));
    t.tm_mon = mon - 1;
    
    t.tm_mday = from_string<int>(substring(dt, 8, 2));
    
    t.tm_hour = (fields[1].size() >= 13) ? from_string<int>(substring(dt, 11, 2)) : 0;
    t.tm_min = (fields[1].size() >= 16) ? from_string<int>(substring(dt, 14, 2)) : 0;
    t.tm_sec = (fields[1].size() >= 19) ? from_string<int>(substring(dt, 17, 2)) : 0;

    const std::time_t tt { timegm(&t) };    

    _t_start = tt;
    _t_end = _t_start + from_string<int>(fields[2]) * 3600; // add the right number of seconds to go one second past the end of the contest
    
    _hours = from_string<int>(fields[2]);
  }

  READ_AND_WRITE(directory);  ///< directory that contains the logs
  READ_AND_WRITE(t_start);    ///< duration of the contest, in hours               
  READ_AND_WRITE(t_end);      ///< time of the start of the contest
  READ_AND_WRITE(hours);      ///< one second past the end of the contest
    
/*! \brief      Is a particular time within the contest period?
    \param  t   time to test
    \return     whether <i>t</i> is within the contest period
*/
  inline bool in_contest_period(const time_t & t) const
    { return (t >= _t_start) and (t < _t_end); }
};

#endif    // DRSCP_H
