// $Id: drscp.h 7 2023-01-02 20:19:59Z n7dr $

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

enum class HF_BAND { B160 = 0,
                     B80,
                     B40,
                     B20,
                     B15,
                     B10,
                     BAD
                   };

static const std::vector<std::string> HF_BAND_STR { "160", "80", "40", "20", "15", "10", "BAD" };

class small_qso;

// forward declarations
HF_BAND band_from_qrg(const int qrg);
auto    build_minilog(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& qsos_per_call) -> std::unordered_map<HF_BAND, base_type<decltype(qsos_per_call)>>;

//std::unordered_map<int /* time */, std::vector<small_qso>::const_iterator> build_time_map(const std::vector<small_qso>& vec);
std::vector<small_qso> build_vec(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& qsos_per_call);

std::unordered_set<std::string> calls_with_unreliable_freq(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos, const std::unordered_set<std::string>& calls_with_no_freq_info);

std::pair<std::vector<small_qso>::const_iterator, std::vector<small_qso>::const_iterator> get_bounds(const int target_minutes, const int minimum_minutes, const int maximum_minutes,
                                                                                                     const int ALLOWED_SKEW, const std::vector<small_qso>& vec);
bool    is_bust(const std::string& call, const std::string& copied) noexcept;
bool    is_stn_running(const std::string& call, const int time, const int qrg, const std::unordered_set<std::string>& tcalls, const std::unordered_set<std::string>& calls_with_no_freq_info,
                      const std::unordered_set<std::string>& calls_with_poor_freq_info, const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos_this_band,
                      const std::vector<small_qso>& all_vec,
                      const std::vector<std::vector<small_qso>::const_iterator>& all_time_map, const int minimum_minutes, const int maximum_minutes,
                      const std::string& ignore_call);
                      
std::unordered_set<std::string> process_band(const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& pruned_qsos_this_band,
                                   const std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos_this_band,
                                   const std::unordered_set<std::string>& calls_with_no_freq_info,
                                   const std::unordered_set<std::string>& calls_with_poor_freq_info,
                                   const int max_time_range,
                                   const std::unordered_set<std::string>& scp_calls);
std::unordered_set<std::string> process_directory(const std::string& dirname);

int remove_qsos_outside_contest_period(std::unordered_map<std::string /* tcall */, std::vector<small_qso>>& all_qsos);

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
  
  int _time { };                    ///< time; initially referenced to the epoch, but converted to minutes from the earliest QSO in the contest
  int _id;                          ///< unique QSO identifier
  
public:

/*! \brief              Constructor
    \param  qso_fields  fields taken from a line in a Cabrillo file
*/
  small_qso(const std::vector<std::string>& qso_fields) :
    _id(qso_id++)
  { if (qso_fields.size() < 9)
    { std::cerr << "ERROR constructing small_qso from short vector" << std::endl;
      exit(-1);
    }
    
    _qrg = from_string<int>(qso_fields[1]);
    _tcall = qso_fields[5];
    _rcall = qso_fields[8];
    _band = band_from_qrg(_qrg);
      
// as we don't have proper support for date/time in gcc 10, all we need is a fake
// counter that advances by unity per minute for the duration of a contest
    struct tm t { };
    time_t t_of_day { };
    
    const std::string dat { qso_fields[3] };
    const std::string utc  { qso_fields[4] };

// see: https://www.epochconverter.com/programming/c
    t.tm_year = from_string<int>(substring(dat, 0, 4)) - 1900;
    t.tm_mon  = from_string<int>(substring(dat, 5, 2)) - 1;
    t.tm_mday = from_string<int>(substring(dat, 8, 2));
    t.tm_hour = from_string<int>(substring(utc, 0, 2));
    t.tm_min  = from_string<int>(substring(utc, 2, 2));
    
    t_of_day = mktime(&t) / 60;         // need only minute, not second
    
    _time = t_of_day;
 
    if (_time < 0)                      // ERROR
    { std::cout << t.tm_year << " | " << t.tm_mon << " | " << t.tm_mday << " | " << t.tm_hour << " | " << t.tm_min << " => " << t_of_day << " or " << _time << std::endl;
      
      std::cout << "dat = " << dat << std::endl;
      std::cout << "utc = " << utc << std::endl;
      
      for (const std::string& field : qso_fields)
        std::cout << field << std::endl;
      
      exit(-1);
    } 
  }

/*! \brief              Constructor
    \param  qso_fields  fields taken from a line in a Cabrillo file
*/
small_qso(const std::vector<std::string_view>& qso_fields)
  { if (qso_fields.size() < 9)
    { std::cerr << "ERROR constructing small_qso from short vector" << std::endl;
      exit(-1);
    }
    
    _qrg = from_string<int>(std::string { qso_fields[1] });
    _tcall = qso_fields[5];
    _rcall = qso_fields[8];
    _band = band_from_qrg(_qrg);
      
// as we don't yet have proper support for date/time, all we need is a fake
// counter that advances by one per minute for the duration of a contest
    struct tm t { };
    time_t t_of_day { };
    
    const std::string dat { qso_fields[3] };
    const std::string utc { qso_fields[4] };

// see: https://www.epochconverter.com/programming/c
    t.tm_year = from_string<int>(substring(dat, 0, 4)) - 1900;
    t.tm_mon  = from_string<int>(substring(dat, 5, 2)) - 1;
    t.tm_mday = from_string<int>(substring(dat, 8, 2));
    t.tm_hour = from_string<int>(substring(utc, 0, 2));
    t.tm_min  = from_string<int>(substring(utc, 2, 2));
    
    t_of_day = mktime(&t) / 60;         // need only minute, not second
    
    _time = t_of_day;
 
    if (_time < 0)
    { std::cout << t.tm_year << " | " << t.tm_mon << " | " << t.tm_mday << " | " << t.tm_hour << " | " << t.tm_min << " => " << t_of_day << " or " << _time << std::endl;
      
      std::cout << "dat = " << dat << std::endl;
      std::cout << "utc = " << utc << std::endl;
      
      for (const auto& field : qso_fields)
        std::cout << field << std::endl;
      
      exit(-1);
    } 
  }

/*! \brief              Constructor
    \param  qso_line    line from a Cabrillo file
*/
  small_qso(const std::string& qso_line)
  { const std::vector<std::string> qso_fields { split_string(qso_line, ' ') };  // assumes has already been squashed
  
    *this = small_qso(qso_fields);
  }

/*! \brief              Constructor
    \param  qso_line    line from a Cabrillo file
*/
  small_qso(const std::string_view& qso_line)
  { const std::vector<std::string_view> qso_fields { split_string_sv(qso_line, ' ') };  // assumes has already been squashed
  
    *this = small_qso(qso_fields);
  }

  READ_AND_WRITE(tcall);
  READ_AND_WRITE(rcall);
  READ(band);
  READ(qrg);
  READ_AND_WRITE(time);
  READ(id);

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
        rv[call1] += call2;
    }
  }
  
  return rv;
}

#endif    // DRSCP_H
