// $Id: drscp.cpp 7 2023-02-03 17:28:36Z n7dr $

// Released under the GNU Public License, version 2
//   see: https://www.gnu.org/licenses/gpl-2.0.html

// Principal author: N7DR

// Copyright owners:
//    N7DR

/*! \file   drscp.cpp

    Program to generate custom SCP (super check partial) files
    
    drscp -dir <directory of contest logs> [-start <start date/time>] [-hrs <duration in hours>]
          [-v] [-l cutoff-count] [-p parallel-number]
          [-tr call to trace] [-tl lower-limit] [-x] [-i]
    
      -start        date/time of the start of the contest: YYYY-MM-DD[THH[:MM[:SS]]]
      -hrs          duration of the contest, in hours
      -v            be verbose
      -l <n>        roughly, the number of times that a call must appear in the logs, even after
                    reasonable precautions have been taken to remove busts. Default 1.
      -p <n>        the number of directories to process simultaneously. Default 1.
      -tr <call>    provide detailed information on the processing of a particular logged call
      -tl <n>       do not automatically include entrants' calls unless they claim at least n QSOs. Default 1.
      -x            generate eXtended SCP output
      -xpc <n>      return only the top n% of most-frequently-seen calls. Default 100.
      -i            display erroneous QSO lines from logs on the standard error stream
 
Notes:
    
    Each contest requires three values: the name of the directory holding the logs,
    the date/time of the start of the contest, and the contest duration, in hours
    
    If drscp is used to process a single contest, then the latter two values may be specified with the
    -start and -hrs parameters respectively.
    
    If <directory of contest logs> begins with the commat character, then the value, without the
    leading character, is treaded as a file that contains a list of space-separated directories,
    start times and durations to process, one contest per line. See the README file for examples.
    
    The -l limit is applied independently to each contest and band.
    
    Regardless of the value of -tl, entrants' calls must also appear in at least one other log.
    
    When using the -xpc option, a strict calculation of "top n%" might well fall in the middle of a number of calls
    with the same number of appearances. In this case, the output includes all calls that appear at least as often
    as the strict value of "top n%" might suggest. 
*/

#include "command_line.h"
#include "count_values.h"
#include "diskfile.h"
#include "drscp.h"
#include "macros.h"
#include "string_functions.h"

#include <algorithm>
#include <iostream>

using namespace std;

constinit int  CUTOFF_LIMIT     { 1 };      ///< will remove calls that appear this many (or fewer) times
constinit int  MAX_PARALLEL     { 1 };      ///< maximum number of directories to process at once
constinit int  TL_LIMIT         { 1 };      ///< do not automatically include entrants' calls unless they claim at least this number of QSOs
constinit int  PC_OUTPUT        { 100 };    ///< percentage of calls to return
constinit bool DISPLAY_BAD_QSOS { false };  ///< whether to display bad QSOs from logs on cerr

constexpr int CLOCK_SKEW     { 2 };     ///< maximum permitted clock skew when comparing logs, in minutes
constexpr int FREQ_SKEW      { 2 };     ///< maximum permitted frequency skew when comparing logs, in kHz
constexpr int RUN_TIME_RANGE { 5 };     ///< half-width of time range for looking for a run, in minutes

constinit atomic<int> processing_directories { 0 };     ///< number of directories currently being processed
constinit bool        tracing                { false }; ///< whether -tr option is in use
constinit bool        verbose                { false }; ///< whether to produce verbose output

string traced_call { };                                 ///< the call being traced

int main(int argc, char** argv)
{ const command_line cl(argc, argv);

  if (cl.parameter_present("-v"))
    verbose = true;
  
  if (!cl.value_present("-dir"s))
  { cerr << "ERROR: no -dir flag present" << endl;
    exit(-1);
  }
    
  const string rawdirname { cl.value("-dir"s) };

  vector<string> dirnames;

  dirnames += rawdirname;         // just one directory

  vector<contest_parameters> params_vec { };

  if (dirnames[0].starts_with('@'))
  { const string filename { substring(dirnames[0], 1) };
  
    if (!file_exists(filename))
    { cerr << "ERROR: file " << filename << " does not exist" << endl;
      exit(-1);
    }
    
    const vector<string> lines { to_lines(read_file(filename)) };
    
    dirnames.clear();
    
    for (const auto& line : lines)
    { if (!line.empty() and !line.starts_with('#') and !contains(line, ' '))
        dirnames += line;
      if (!line.empty() and !line.starts_with('#') and contains(line, ' '))
        params_vec += contest_parameters { line };
    }
  }
  else      // an actual directory
  { if (!cl.value_present("-start"))
    { cerr << "ERROR: missing -start parameter" << endl;
      exit(-1);
    }
    
    if (!cl.value_present("-hrs"))
    { cerr << "ERROR: missing -hrs parameter" << endl;
      exit(-1);
    }
    
    params_vec += contest_parameters { dirnames[0] + " " + cl.value("-start") + " " + cl.value("-hrs") };
  }

  for (const auto& cp : params_vec)
  { if (!directory_exists(cp.directory(), LINKS::INCLUDE))
    { cerr << "ERROR: raw directory " << cp.directory() << " does not exist" << endl;
      exit(-1);
    }
  }
  
  if (cl.value_present("-l"s))
    CUTOFF_LIMIT = from_string<int>(cl.value("-l"s));
    
  if (verbose)
    cout << "cutoff limit = " << CUTOFF_LIMIT << endl;

  if (cl.value_present("-p"s))
    MAX_PARALLEL = from_string<int>(cl.value("-p"s));
    
  if (verbose)
    cout << "number of directories to process in parallel = " << MAX_PARALLEL << endl;

  if (cl.value_present("-tr"s))
  { traced_call = to_upper(cl.value("-tr"));
    tracing = true;
  }
  
  if (cl.value_present("-tl"s))
    TL_LIMIT = from_string<int>(cl.value("-tl"));

  if (verbose)
    cout << "entrants' calls automatically included only if they claim at least " << TL_LIMIT << ((TL_LIMIT == 1) ? " QSO" : " QSOs") << endl;

  const bool xscp { cl.parameter_present("-x"s) };      // whether to generate XSCP output

  if (verbose)
    cout << "output format is: " << (xscp ? "XSCP" : "SCP") << endl;
    
  if (cl.value_present("-xpc"s))
  { PC_OUTPUT = from_string<int>(cl.value("-xpc"));
  
    if (verbose)
      cout << "top " << PC_OUTPUT << " of values will be returned" << endl;
  }
  
  DISPLAY_BAD_QSOS = cl.parameter_present("-i"s);       // whether to print bad QSOs from logs
  
  CALL_MAP xscp_calls(compare_calls);                   // the calls to be printed
 
  list<future<CALL_MAP>> futures;
 
// queue all the directories for processing, as resources become available 
  for (const contest_parameters& cp : params_vec)
  { if (verbose)
      cout << "queuing directory " << cp.directory() << " for processing when a thread becomes free" << endl;
  
    while (processing_directories == MAX_PARALLEL)
    { for (auto it { futures.begin() }; it != futures.end(); ++it)
      { if (it->wait_for(0s) == future_status::ready)               // if a thread has completed
        { xscp_calls += move(it->get());
 
          futures.erase(it);
          processing_directories--;
 
          break;
        }
      }
        
      this_thread::sleep_for(1s);
    }
    
    futures.emplace(futures.end(), async(std::launch::async, process_directory, cp));
    
    if (verbose)
      cout << "started processing directory " << cp.directory() << endl;
    
    processing_directories++;
  }
  
  while (!futures.empty())
  { for (auto it { futures.begin() }; it != futures.end(); ++it)
    { if (it->wait_for(0s) == future_status::ready)
      { xscp_calls += move(it->get());
        
        futures.erase(it);
        processing_directories--;

        break;
      }
    }
      
    if (!futures.empty())
      this_thread::sleep_for(1s);
  }

// possibly prune the list for output
  if (PC_OUTPUT != 100)
  { vector<int> values;
    values.reserve(xscp_calls.size());
    
    FOR_ALL(xscp_calls, [&values] (const auto& pr) { values += pr.second; });
    
    erase_if(xscp_calls, [val_limit = value_line(values, PC_OUTPUT)] (const auto& pr) { return (pr.second < val_limit); });
  }

// we are finished; output the list of [X]SCP calls, one per line
  if (xscp)
    FOR_ALL(xscp_calls, [] (const auto& pr) { cout << pr.first << " " << pr.second << endl; });
  else
    FOR_ALL(xscp_calls, [] (const auto& pr) { cout << pr.first << endl; });
  
  return 1;
}

/*  \brief          Is a copied call a bust of another call?
    \param  call    target (correct) call
    \param  copied  copied call
    \return         whether <i>copied</i> is a plausiblg bust of <i>call</i>
    
    This is shamelessly copied from contest_statistics, with no substantive changes
*/
bool is_bust(const string& call, const string& copied) noexcept
{ if (call == copied)
    return false;                // not a bust if it's copied OK

  if ( abs(ssize(call) - ssize(copied)) >= 2 )
    return false;                // not a bust if the lengths differ by 2 or more

  if ( abs(ssize(call) - ssize(copied)) == 1 )  // lengths differ by unity
  { const string& longer  { (call.length() > copied.length() ? call : copied) };
    const string& shorter { (call.length() > copied.length() ? copied : call) };

    if (contains(longer, shorter))
      return true;

// is the bust in the form of an additional character, or a missing character, somewhere in the call?
    for (size_t posn { 1 }; posn < longer.length() - 1; ++posn)
    { const string tmp { longer.substr(0, posn) + longer.substr(posn + 1) };

      if (tmp == shorter)
        return true;
    }

    return false;
  }

// call and copied are the same length; do they differ by exactly one character?
  int differences { 0 };

  for (size_t posn { 0 }; posn < call.length(); ++posn)
  { if (call[posn] != copied[posn])
      differences++;
  }

  if (differences == 1)
    return true;

// is there a character inversion?
  for (size_t posn { 0 }; posn < call.length() - 1; ++posn)
  { string call_tmp { call };

    swap(call_tmp[posn], call_tmp[posn + 1]);

    if (call_tmp == copied)
      return true;
  }

  return false;
}

/*  \brief                  Split a log into per-band minilogs
    \param  qsos_per_call   all the QSOs for each call
    \return                 <i>qsos_per_call</i> divided into per-band logs
*/
auto build_minilog(const unordered_map<string /* tcall */, vector<small_qso>>& qsos_per_call) -> unordered_map<HF_BAND, base_type<decltype(qsos_per_call)>> 
{ unordered_map<HF_BAND, base_type<decltype(qsos_per_call)>> rv;
  
  for (const auto& [ tcall, qsos ] : qsos_per_call)
  { for (const small_qso& qso : qsos)
    { const HF_BAND band_qsos { qso.band() };
      const string& tcall     { qso.tcall() };
      
      rv[band_qsos][tcall] += qso;
    }
  }
  
  return rv;
}

/*  \brief                  Convert a minilog into a time-ordered vector
    \param  qsos_per_call   all the QSOs for each call
    \return                 <i>qsos_per_call</i> organised as a single chronological vector
*/
vector<small_qso> build_vec(const unordered_map<string /* tcall */, vector<small_qso>>& qsos_per_call)
{ vector<small_qso> rv;            // all QSOs on this band
  
// calculate the putative size
  size_t rv_size { 0 };
  
  for (const auto& [ tcall, qsos_this_call ] : qsos_per_call)       // structured binding in lambda is not permitted
    rv_size += qsos_this_call.size();
    
  rv.reserve(rv_size);
    
  for (const auto& [ tcall, qsos_this_call ] : qsos_per_call)
    FOR_ALL(qsos_this_call, [&rv] (const small_qso& qso) { rv += qso; } );

// put in chronological order
  SORT(rv);
  
  return rv;
}

/*  \brief                  Return all the tcalls in all minilogs
    \param  qsos_per_call   all the QSOs for each call
    \return                 all the tcalls that appear in all the QSOs
*/
unordered_set<string> tcalls(const unordered_map<string /* tcall */, vector<small_qso>>& qsos_per_call)
{ unordered_set<string> rv { };

  for (const auto& [ tcall, qso_vec ] : qsos_per_call)
    rv += tcall;

  return rv;
}

/*  \brief          Build a map that converts from time to the iterator for the first element for that minute in a chronologically-sorted QSO vector
    \param  vec     chronologically-sorted QSO vector
    \return         map that converts from time (for each minute) to the iterator for the first element of <i>vec</i>for that minute
*/
vector<vector<small_qso>::const_iterator> time_map(const vector<small_qso>& vec, const int max_rel_mins)
{ vector<vector<small_qso>::const_iterator> rv;

  if (vec.empty())                  // should never be true
    return rv;
  
  const int time_map_size { static_cast<int>(max_rel_mins) + 1 };   // note the +1
  
  rv.reserve(time_map_size);

  vector<small_qso>::const_iterator last_start { vec.cbegin() };

  for (long int minutes { 0 }; minutes <= max_rel_mins; ++minutes)
  { rv += lower_bound(last_start, vec.end(), minutes, [] (const auto& element, const auto& target) { return (element.rel_mins() < target); });
    last_start = rv[rv.size() - 1];
  }
    
  rv += vec.end();

  return rv;
}

/*! \brief                              Generate the SCP calls from containers of pruned and all QSOs
    \param  pruned_qsos_this_band       the pruned QSOs (for a band)
    \param  all_qsos_this_band          all the QSOs (for the band)
    \param  calls_with_no_freq_info     the calls that have no reliable frequency info in the log
    \param  calls_with_poor_freq_info
    \param  max_rel_mins                the number of minutes in the contest - 1
    \return                             the SCP calls after adding those from the containers
*/
unordered_set<string> process_band(const unordered_map<string /* tcall */, vector<small_qso>>& pruned_qsos_this_band,
                                   const unordered_map<string /* tcall */, vector<small_qso>>& all_qsos_this_band,
                                   const unordered_set<string>& calls_with_no_freq_info,
                                   const unordered_set<string>& calls_with_poor_freq_info,
                                   const int max_rel_mins)
{ 
// put all the qsos on this band, and all the pruned qsos, into vectors
  vector<small_qso> pruned_vec { build_vec(pruned_qsos_this_band) };

  const vector<small_qso>     all_vec    { build_vec(all_qsos_this_band) };
  const string                band_str   { HF_BAND_STR.at(static_cast<int>(all_vec[0].band())) + "m" }; // string to be used to identify the band in output
  const unordered_set<string> all_tcalls { tcalls(all_qsos_this_band) };                                // all the tcalls on this band

/*  \brief          Are two frequencies approximately the same?
    \param  qso1    QSO #1
    \param  qso2    QSO #2
    \param  def     whether calls with no detailed frequency info suggests a frequency match
    \return         whether <i>qso1</i> and <qso2> are reoughly on the same frequency (to within FREQ_SKEW kHz)
*/
  auto frequency_match = [&calls_with_no_freq_info, &calls_with_poor_freq_info] (const small_qso& qso1, const small_qso& qso2, const bool def) 
    { if (def)
        return ( (calls_with_no_freq_info.contains(qso1.tcall()) or calls_with_no_freq_info.contains(qso2.tcall())) or 
                 (calls_with_poor_freq_info.contains(qso1.tcall()) or calls_with_poor_freq_info.contains(qso2.tcall())) or 
                 (abs(qso1.qrg() - qso2.qrg()) <= FREQ_SKEW) );
      else
// this is a fudge -- some stations have freq info for only SOME QSOs, (these stns are in calls_with_poor_freq_info).
// This might mischaracterise QSOs close to the band edge, but I think that that's the lesser of the two evils      
        return ( (!calls_with_no_freq_info.contains(qso1.tcall()) and !calls_with_no_freq_info.contains(qso2.tcall())) and 
 //                (!calls_with_poor_freq_info.contains(qso1.tcall()) and !calls_with_poor_freq_info.contains(qso2.tcall())) and
                 (abs(qso1.qrg() - qso2.qrg()) <= FREQ_SKEW) ); 
    };

// look for specific QSO busts, where the frequency and time in two logs match, and an rcall is a bust of a tcall
  unordered_set<int> ids_to_remove;
  
// go through the pruned log, minute by minute; start by building maps from times to vector elements
  const vector<vector<small_qso>::const_iterator> all_time_map    { time_map(all_vec, max_rel_mins) };
  const vector<vector<small_qso>::const_iterator> pruned_time_map { time_map(pruned_vec, max_rel_mins) };
  
  for (int target_rel_mins { 0 }; target_rel_mins <= max_rel_mins; ++target_rel_mins)
  { const time_t lower_target_minutes { max(target_rel_mins - CLOCK_SKEW, 0) };
    const time_t upper_target_minutes { min(target_rel_mins + CLOCK_SKEW, max_rel_mins) };

// create vector of (pruned) rcalls that are targets for this exact minute
    vector<small_qso> pruned_rcall_targets;

    copy(pruned_time_map.at(target_rel_mins), pruned_time_map.at(target_rel_mins + 1), back_inserter(pruned_rcall_targets));     // these are all the pruned rcalls during the target minute
    
// look for matches
    for (const small_qso& rqso : pruned_rcall_targets)
    { const auto it { find_if(all_time_map.at(lower_target_minutes), all_time_map.at(upper_target_minutes + 1),
                               [&frequency_match, &rqso] (const small_qso& tqso) 
                                 { return (frequency_match(tqso, rqso, true) and
                                            ((is_bust(tqso.tcall(), rqso.rcall()) and (tqso.rcall() == rqso.tcall())) or 
                                              (is_bust(rqso.tcall(), tqso.rcall()) and (is_bust(tqso.tcall(), rqso.rcall()))))); })
                    };
                    
      if (it != all_time_map.at(upper_target_minutes + 1))  // end() for the all_vec vector
      { ids_to_remove += rqso.id();
        
        if (verbose)
          cout << band_str << ": marked for removal: " << rqso << "; tcall match = " << *it << endl;
          
        if (tracing and (rqso.rcall() == traced_call))
          cout << band_str << ": traced call " << traced_call << " marked for removal: " << rqso << "; tcall match = " << *it << endl;
      }
    }  
  }
  
  if (verbose)
    cout << band_str << ": number of QSO IDs to remove: " << ids_to_remove.size() << endl;
  
// remove the marked QSOs
  erase_if(pruned_vec, [&ids_to_remove] (const small_qso& qso) { return ids_to_remove.contains(qso.id()); });
  
  if (verbose)
    cout << band_str << ": current number of QSOs in pruned_vec = " << pruned_vec.size() << endl;

  if (tracing)
  { int counter { 0 };
  
    cout << band_str << ": Remaining traced QSOs after initial removal: " << endl;
  
    FOR_ALL(pruned_vec, [band_str, &counter] (const small_qso& qso) { if (qso.rcall() == traced_call)
                                                                     { cout << "  " << band_str << ": " << qso << endl;
                                                                       counter++;
                                                                     }
                                               });
    cout << band_str << ": Pruned number of QSOs containing traced call = " << counter << endl;
  }

  ids_to_remove.clear();        // reset, so can be repopulated

/* handle the following situation:
   A and B are entrants
   A is running
   B claims a QSO with a bust of A
   A contains neither B nor a bust of B at the denoted time and frequency
   
   Below, A is the running station; B is the one with the bust in the log
*/
  { for (const auto& qso : pruned_vec)
    { for (const auto& tcall : all_tcalls)
      { if (!ids_to_remove.contains(qso.id()))      // don't keep going once we know to remove it
        { if (is_bust(tcall, qso.rcall()))
          { const bool running { is_stn_running(tcall, qso.rel_mins(), qso.qrg(), all_tcalls, calls_with_no_freq_info, calls_with_poor_freq_info, all_qsos_this_band, all_vec,
                                 all_time_map, 0, max_rel_mins, qso.tcall()) };
           
            if (running)
            { ids_to_remove += qso.id();
        
              if (verbose)
                cout << band_str << ": marked for removal because unbusted rcall is running: " << qso << "; unbusted rcall = " << tcall << endl;
          
              if (tracing and (qso.rcall() == traced_call))
                cout << band_str << ": traced call " << traced_call << " marked for removal: " << qso << "; tcall match = " << tcall << endl; 
            }
          }          
        }
      }
    }
  }

// remove the marked QSOs
  if (!ids_to_remove.empty())
  { if (verbose)
      cout << "removing " << ids_to_remove.size() << " QSOs for stations determined to be running" << endl;
      
    erase_if(pruned_vec, [&ids_to_remove] (const small_qso& qso) { return ids_to_remove.contains(qso.id()); });
  }

  if (verbose)
    cout << "current number of QSOs in pruned_vec = " << pruned_vec.size() << endl;

  if (tracing)
  { int counter { 0 };
  
    cout << band_str << ": Remaining traced QSOs after removing busts of running stations: " << endl;
  
    FOR_ALL(pruned_vec, [band_str, &counter] (const small_qso& qso) { if (qso.rcall() == traced_call)
                                                                     { cout << "  " << band_str << ": " << qso << endl;
                                                                       counter++;
                                                                     }
                                               });
    cout << band_str << ": Pruned number of QSOs containing traced call = " << counter << endl;
  }

/* now go through pruned_vec, and for each rcall look to see if it's a bust
   of a non-entrant (i.e., not a tcall) rcall that is running on that frequency
   
   This will be somewhat rare, as non-entrants typically do not run.
*/
  if (verbose or tracing)
    cout << band_str << ": now to look for non-entrant busts" << endl;

// build pseudo-logs of rcalls
  unordered_map<string /* rcall */, vector<small_qso> /* rcall log */> rcall_logs;
  unordered_set<string>                                                rcalls;
    
  FOR_ALL(pruned_vec, [&rcalls, &rcall_logs] (const small_qso& qso) { rcall_logs[qso.rcall()] += qso; 
                                                                      rcalls += qso.rcall();
                                                                    });

  for (auto& [ rcall, rcall_log ] : rcall_logs)       // sort each rcall log chronologically; C++ does not permit lambdas with structured binding
    SORT(rcall_log);
  
  if (verbose)
    cout << band_str << ": Number of rcall logs = " << rcall_logs.size() << endl;
 
// this can't be const as [rcall] might create an empty unordered_set later
  unordered_map<string /* call */, unordered_set<string> /* possible_busts */> possible_rcall_busts { possible_busts(rcalls) }; // all the bust permutations in <i>rcalls</i>

// count the number of times each remaining rcall appears
  count_values<string> histogram;
  
  FOR_ALL(pruned_vec, [&histogram] (const small_qso& qso) { histogram += qso.rcall(); });

// invert the histogram, in order of greatest count to least
  const auto inv_histogram { histogram.sorted_invert<set<string>, greater<int>>() };
  
  auto inv_histogram_it { inv_histogram.begin() };
  int  counter          { 0 };
  
  ids_to_remove.clear();
  
  while (inv_histogram_it != inv_histogram.end())
  { if (verbose)
      cout << band_str << ": index = " << counter << ", count : " << inv_histogram_it->first << endl;

    const set<string> rcalls_this_count { inv_histogram_it->second };
  
    if (verbose)
      cout << band_str << ": number of rcalls = " << rcalls_this_count.size() << endl;
  
    for (const auto& rcall : rcalls_this_count)
    { if (verbose)
        cout << band_str << ": rcall = " << rcall << endl;
 
       if (tracing and (rcall == traced_call))
         cout << band_str << ": testing " << rcall << " under inv_histogram count = " << inv_histogram_it->first << endl;
 
      vector<small_qso> log_of_rcall_and_busts { rcall_logs[rcall] };   // start with the log of this rcall
 
      if (tracing and (traced_call == rcall))
      { cout << band_str << ": all QSOs with this rcall: " << endl;
        FOR_ALL(rcall_logs.at(rcall), [&band_str] (const small_qso& qso) { cout << "  " << band_str << ": " << qso << endl; });
      }

// for each of the QSOs in rcall_logs[rcall], see if it's a run QSO of a bust of rcall
      const unordered_set<string> rcall_busts { possible_rcall_busts[rcall] };  // all the busts of this rcall; do not use .at() here, as [rcall] will have no entry if there are no busts of rcall 

      if (tracing and (rcall == traced_call))
      { cout << band_str << ": number of rcall busts = " << rcall_busts.size() << endl;
        
        CALL_SET ordered_rcall_busts(compare_calls);
        
        FOR_ALL(rcall_busts,         [&ordered_rcall_busts] (const string& rcall_bust) { ordered_rcall_busts += rcall_bust; });
        FOR_ALL(ordered_rcall_busts, [&band_str]            (const string& rcall_bust) { cout << band_str << ":  " << rcall_bust << endl; });
      }
    
      FOR_ALL(rcall_busts, [&log_of_rcall_and_busts, &rcall_logs] (const string& rcall_bust) { log_of_rcall_and_busts += rcall_logs[rcall_bust]; } );
      SORT(log_of_rcall_and_busts);             // put the combined log for rcall and all its busts into chronological order

      if (tracing and (rcall == traced_call))
      { cout << "combined log for " << rcall << " and all its busts:" << endl;
        FOR_ALL(log_of_rcall_and_busts, [&band_str] (const small_qso& qso) { cout << band_str << ":  " << qso << endl; });
      }

      for (const small_qso& rqso : rcall_logs[rcall])
      { if (tracing and (rcall == traced_call))
          cout << band_str << ": testing whether QSO is in a run: " << rqso << endl;

        const auto [ lb, ub ] { get_bounds(rqso.rel_mins(), 0, max_rel_mins, RUN_TIME_RANGE, log_of_rcall_and_busts) };
        
        if (verbose or (tracing and (rcall == traced_call)))
        { const int target_minutes       { rqso.rel_mins() };
          const int lower_target_minutes { max(target_minutes - RUN_TIME_RANGE, 0) };
          const int upper_target_minutes { min(target_minutes + RUN_TIME_RANGE, max_rel_mins) }; 
          const int low_rel_mins         { lb->rel_mins() };
          const int high_rel_mins        { prev(ub)->rel_mins() }; 
        
          cout << band_str << ": time range: " << low_rel_mins << " to " << high_rel_mins
               << " for target time = " << target_minutes << "; lower target = " << lower_target_minutes << ", upper target = " << upper_target_minutes << endl;
        }

        const bool run_qso { ANY_OF(lb, ub, [&calls_with_no_freq_info, &frequency_match, &rcall, &rqso] (const small_qso& qso) 
                                      { if (qso.rcall() == rcall) // select only ones with different call
                                          return false;
                                                                            
                                        if (verbose and frequency_match(qso, rqso, false))
                                        { cout << "MATCH: " << qso << " | " << rqso << endl;
                                          cout << "  freq info1: " << calls_with_no_freq_info.contains(qso.tcall())  << endl;
                                          cout << "  freq info2: " << calls_with_no_freq_info.contains(rqso.tcall())  << endl;
                                          cout << "  comparison: " << (abs(qso.qrg() - rqso.qrg()) <= 2) << endl;
                                        }
                                                                            
                                        return frequency_match(qso, rqso, false);        // use frequency_match lambda
                                      } ) };
          
        if (verbose or (tracing and (rcall == traced_call)))
          cout << band_str << ": run_qso = " << boolalpha << run_qso << endl;

        if (run_qso)
        { ids_to_remove += rqso.id();
         
          if (tracing and (rcall == traced_call))
            cout << band_str << ": traced call " << traced_call << " marked for removal: " << rqso << endl;
        }
      }
    }
    
    inv_histogram_it++;
    counter++;
  }
  
// remove the QSOs marked for removal
  erase_if(pruned_vec, [&ids_to_remove] (const small_qso& qso) { return ids_to_remove.contains(qso.id()); });

  if (verbose)
    cout << band_str << ": Number of remaining calls after processing busts for possible runs = " << pruned_vec.size() << endl;

// regenerate the histogram and remove the calls with too few occurrences
  histogram.clear();

  FOR_ALL(pruned_vec, [&histogram] (const small_qso& qso) { histogram += qso.rcall(); });

// remove all the rcalls that are at or below CUTOFF_LIMIT (default = 1)
  if (verbose)
  { cout << band_str << ": Erasing calls below CUTOFF_LIMIT ( = " << CUTOFF_LIMIT << " )" << endl;

    for (auto& [ rcall, count ] : histogram)
    { if (count <= CUTOFF_LIMIT)
      { cout << band_str << ": Erasing call: " << rcall << endl;
         
        erase_if(pruned_vec, [&rcall] (const small_qso& qso) { return (qso.rcall() == rcall); });
      }
    }
    
    cout << band_str << ": final number of QSOs in pruned_vec = " << pruned_vec.size() << endl;
  }  
  else  // not verbose
    erase_if(pruned_vec, [&histogram] (const small_qso& qso) { return (histogram.at(qso.rcall()) <= CUTOFF_LIMIT); });

// add the remaining rcalls to local_scp_calls
  unordered_set<string> local_scp_calls { };

  FOR_ALL(pruned_vec, [&local_scp_calls] (const small_qso& qso) { local_scp_calls += qso.rcall(); } );   // NB will try to add many times, but should be fast

  if (verbose)
  { FOR_ALL(local_scp_calls, [] (const string& call) { cout << call << endl; } );
    cout << band_str << ": final number of SCP calls = " << local_scp_calls.size() << endl;
  }
  
  return local_scp_calls;
}

/*! \brief      Process all the logs in a directory
    \param  cp  directory, start and duration
    \return     the SCP calls for the logs in directory <i>dirname </i>
*/
CALL_MAP process_directory(const contest_parameters& cp)
{ const string& dirname { cp.directory() };

  unordered_set<string>                                scp_calls;               // the calls in the SCP list
  unordered_map<string /* tcall */, vector<small_qso>> all_qsos;                // all QSOs as recorded in the logs
  unordered_map<string /* tcall */, vector<small_qso>> pruned_qsos;             // some QSOs removed, removing more as we go along
  int                                                  n_valid_logs    { 0 };
  int                                                  max_rel_mins    { cp.hours() * 60 - 1 };   // maximum legal value

  const string_view legal_chars { "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890/"sv };  // all the chars that are legal in callsigns
  
  for (const string& logfile_name : files_in_directory(dirname, LINKS::INCLUDE))
  { unordered_map<string /* tcall */, vector<small_qso>> tcall_qsos;    // do not assume that the tcall doesn't change within the log

    const string prepared_content { to_upper(squash(replace_char(read_file(logfile_name), '\t', ' '))) }; // force this to be an lvalue

    for (const auto line : to_lines_sv(prepared_content))       // a rather minor improvement in efficientcy
    { if (line.starts_with("QSO:"sv))
      { small_qso qso { line };
 
        if (!cp.in_contest_period(qso.time()))
          continue;
        
        qso.rel_mins( (qso.time() - cp.t_start()) / 60 );       // minutes since the start of the contest
        
        if (!qso.tcall().empty())                               // if we successfully constructed a valid QSO
        { qso.tcall(remove_from_end(qso.tcall(), "/QRP"s));     // obviously
          qso.rcall(remove_from_end(qso.rcall(), "/QRP"s));     //    do.
          qso.tcall(remove_from_end(qso.tcall(), "/QRPP"s));    // yup... some people do this
          qso.rcall(remove_from_end(qso.rcall(), "/QRPP"s));

          const string_view qt { qso.tcall() };
          const string_view qr { qso.rcall() };
        
          if ((qt.size() < 3) or (qr.size() < 3))
            continue;

          if ( (qt.find_first_not_of(legal_chars) != string::npos) or (qr.find_first_not_of(legal_chars) != string::npos) )
            continue;
          
          if (qso.tcall() == qso.rcall())                     // some people "work themselves" to mark bad QSOs but to keep serial numbers intact
            continue;
 
          if (tracing and (qso.rcall() == traced_call))
            cout << "Read traced call from log: " << qso << endl;

          tcall_qsos[qso.tcall()] += move(qso);
        }
      }
    }

    if (!tcall_qsos.empty())
    { n_valid_logs++;
      
      for (auto& [ tcall, qsos ] : tcall_qsos)
      { all_qsos += { tcall, qsos };
 
        if (ssize(qsos) >= TL_LIMIT)
          scp_calls += tcall;                             // put all the tcalls into scp_calls.
        else
        { if (verbose)
            cout << logfile_name << ": log size too small for tcall: " << tcall << endl;
        }
      }
    }
  }
  
  if (verbose)
    cout << dirname << ": total number of logs with valid QSOs = " << n_valid_logs << endl;

  if (n_valid_logs == 0)
  { cerr << "ERROR: no valid received logs" << endl;
    exit(-1);
  }
  
  if (verbose)
    cout << dirname << ": Number of tcalls = " << scp_calls.size() << endl;

// ensure that all logs are in chronological order
  for (auto& [tcall, qsos] : all_qsos)
    SORT(qsos);

  const int max_time_range { cp.hours() * 60 };   // number of minutes in the contest    

  if (verbose)
    cout << dirname << ": minutes in contest = " << max_time_range << endl;

// start with the pruned QSOs being identical to all_qsos
  pruned_qsos = all_qsos;

// prune all the QSOs for which the rcall is is a known tcall (regardless of whether anything else matches)
// also put those rcalls into the output map
  CALL_MAP scp_cm(compare_calls);
  
  for ( auto& [ tcall, qsos ] : pruned_qsos )
  { for (const auto& qso : qsos)
      if (scp_calls.contains(qso.rcall()))
        scp_cm += qso.rcall();
        
    erase_if(qsos, [scp_calls] (const small_qso& qso) { return scp_calls.contains(qso.rcall()); } );
  }

  if (verbose)
    cout << dirname << ": nlogs = " << all_qsos.size() << endl;
  
// remove any logs for which all the rcalls are already in scp_calls
  erase_if(pruned_qsos, [] (const auto& pr) { return pr.second.empty(); } );

  if (verbose)
    cout << dirname << ": pruned nlogs after removing rcalls in scp_calls = " << pruned_qsos.size() << endl;

// at some point we shall need a container of calls that do not have frequency info in the log
  unordered_set<string> calls_with_no_freq_info;
  
  for (const auto& [ tcall, qsos ] : all_qsos)
  { static const set<int> default_band_freq { 1800, 3500, 7000, 14000, 21000, 28000 };  // if all frequencies are from this set, then there is no frequency info
  
    if (ALL_OF(qsos, [] (const small_qso& qso) { return default_band_freq.contains(qso.qrg()); }))
      calls_with_no_freq_info += tcall; 
  }

  if (verbose)
    cout << dirname << ": Number of logs with no frequency info = " << calls_with_no_freq_info.size() << endl;

  const unordered_set<string> calls_with_poor_freq_info { calls_with_unreliable_freq(all_qsos, calls_with_no_freq_info) };
  
  if (verbose)
    cout << dirname << ": Number of logs with unreliable frequency info = " << calls_with_poor_freq_info.size() << endl;

  if (tracing)
  { cout << "In chronological order, all QSOs with traced call: " << traced_call << endl;
    
    int counter { 0 };
    FOR_ALL(build_vec(all_qsos), [&counter] (const small_qso& qso) { if (qso.rcall() == traced_call)
                                                                     { cout << "  " << qso << endl;
                                                                       counter++;
                                                                     }
                                                                   });
                                                                   
    cout << "total number of QSOs containing traced call = " << counter << endl;
    
    counter = 0;
    
    cout << "In chronological order, all remaining QSOs with traced call: " << traced_call << endl;
    FOR_ALL(build_vec(pruned_qsos), [&counter] (const small_qso& qso) { if (qso.rcall() == traced_call)
                                                                          { cout << "  " << qso << endl;
                                                                            counter++;
                                                                          }
                                                                      });
                                                              
    cout << "pruned number of QSOs containing traced call = " << counter << endl;
  }

// remove QSOs for which the rcall appears to be a bust of another station's tcall

// build minilogs for each band and call
  const unordered_map<HF_BAND, decltype(all_qsos)> all_per_band_qsos    { build_minilog(all_qsos) };
  const unordered_map<HF_BAND, decltype(all_qsos)> pruned_per_band_qsos { build_minilog(pruned_qsos) };

  vector<future<unordered_set<string>>> futures;
  vector<unordered_set<string>>         out_calls;

  for (const HF_BAND this_band : vector { HF_BAND::B160, HF_BAND::B80, HF_BAND::B40, HF_BAND::B20, HF_BAND::B15, HF_BAND::B10 } )
//  for (const HF_BAND this_band : vector { HF_BAND::B160 } )
    if (pruned_per_band_qsos.contains(this_band) and all_per_band_qsos.contains(this_band))                         // not every contest permits every band
      futures.emplace_back(async(std::launch::async, process_band, ref(pruned_per_band_qsos.at(this_band)), ref(all_per_band_qsos.at(this_band)), ref(calls_with_no_freq_info), 
                                                                   ref(calls_with_poor_freq_info), max_rel_mins));
  
  for (int n { 0 }; n < ssize(futures); ++n)
    out_calls += move(futures[n].get());

  unordered_set<string> returned_calls;

  FOR_ALL(out_calls, [&returned_calls] (const auto& band_calls) { returned_calls += move(band_calls); });

  if (verbose)
    cout << "total number of SCP calls = " << returned_calls.size() << endl;

  if (tracing)
    cout << "call " << traced_call << " IS " << (returned_calls.contains(traced_call) ? "" : "NOT ") << "in initial SCP list" << endl;
  
  if (verbose)
    cout << "Finished processing directory: " << dirname << endl;
  
// fill the output map
  CALL_MAP rv(compare_calls);   // start with the map from tcalls
  
  rv += scp_cm;   // start with the map from tcalls

  for (auto& [ tcall, qsos ] : all_qsos)
  { for (auto& qso : qsos)
    { if (returned_calls.contains(qso.rcall()))
        rv += qso.rcall();
    }
  }
    
  return rv;
}

/*! \brief                              Return the calls whose logged frequencies seem to be unreliable
    \param  all_qsos                    all the QSOs, per entrant's call
    \param  calls_with_no_freq_info     entrants whose logged frequency information is useless
    \return                             calls of stations whose logged frequency appears unreliable
*/
unordered_set<string> calls_with_unreliable_freq(const unordered_map<string /* tcall */, vector<small_qso>>& all_qsos, const unordered_set<string>& calls_with_no_freq_info)    //;                // all QSOs as recorded in the logs
{ unordered_set<string> rv;

  using BAND_TIME_FREQ = tuple<HF_BAND, int, int>;
  
  unordered_map<string /* tcall */, unordered_map<string /* rcall */, vector<BAND_TIME_FREQ>>> worked; 

  for (const auto& [ tcall, qsos ] : all_qsos)
  { if (!calls_with_no_freq_info.contains(tcall))           // neither tcall nor rcall may be a call with no frequency info
    { unordered_map<string /* rcall */, vector<BAND_TIME_FREQ>> worked_by_this_tcall;
  
      for (const auto& qso : qsos)
      { const string& rcall { qso.rcall() };
      
        if (!calls_with_no_freq_info.contains(rcall))               // neither tcall nor rcall may be a call with no frequency info
          if (all_qsos.find(rcall) != all_qsos.cend())              // rcall is a tcall in the map
            worked_by_this_tcall[rcall] += BAND_TIME_FREQ { band_from_qrg(qso.qrg()), qso.rel_mins(), qso.qrg() };
      }
    
      worked[tcall] = move(worked_by_this_tcall);
    }
  }

// all logged QSOs between entrants are now cross-indexed
  using TOTAL_GOOD = pair<int, int>;                /* keep count of total and good QSOs */
  
  unordered_map<string /* tcall */, TOTAL_GOOD> accumulated_counts;
  
  for (const auto& [ tcall, rcall_map ] : worked)   // rcall_map is: unordered_map<string /* rcall */, vector<BAND_TIME_FREQ>
  { int total { 0 };
    int good  { 0 };
    
    for (const auto& [ rcall, btf_vec ] : rcall_map)
    { for (const auto [ tband, ttime, tfreq ] : btf_vec)        // tcall, rcall, time and freq are now all accessible; look for the reverse QSO
      { const auto& rcall_worked { worked.at(rcall) };            // rcall_worked is: unordered_map<string /* rcall */, vector<BAND_TIME_FREQ>>
        
        auto it { rcall_worked.find(tcall) };
        
        if (it != rcall_worked.cend())              // found the tcall QSOs in the rcall data
        { const auto& [ call, vtf ] { *it };

          for (const auto [ rband, rtime, rfreq ] : vtf)
          { if ( (tband == rband) and (abs(ttime - rtime) < RUN_TIME_RANGE) )           // within five minutes on the right band
            { total++;
            
              if (abs(tfreq - rfreq) < FREQ_SKEW)                               // within 2 kHz
                good++;
            }
          }
        }
      }
    }
    
    accumulated_counts[tcall] = { total, good };
  }

  for (auto& [ tcall, total_good ] : accumulated_counts)
  { const auto [ total, good ] { total_good };
    
    if (total != 0)
    { const float good_fraction { float(good) / total };
    
      if (good_fraction < 0.9)                      // 0.9 is arbitrary, but seems reasonable for defining unreliable logging of frequency 
        rv += tcall;
    }
  }

  return rv;
}

/*! \brief          Return the band associated with a frequency
    \param  qrg     frequency, in kHz
    \return         the band in which the frequency <i>qrg</i> lies
    
    Throws an exception if <i>qrg</i> does not appear to be in a contest band
*/
HF_BAND band_from_qrg(const int qrg)
{ if ((qrg >= 1800) and (qrg <= 2000))
    return HF_BAND::B160;
      
  if ((qrg >= 3500) and (qrg <= 4000))
    return HF_BAND::B80;
      
  if ((qrg >= 7000) and (qrg <= 7300))
    return HF_BAND::B40;    

  if ((qrg >= 14000) and (qrg <= 14350))
    return HF_BAND::B20;

  if ((qrg >= 21000) and (qrg <= 21450))
    return HF_BAND::B15;
      
  if ((qrg >= 28000) and (qrg <= 29700))
    return HF_BAND::B10;
      
  throw exception();
}

/*! \brief                              Determine whether a station is running at a particular time and on a particular frequency
    \param  call                        call of the target station
    \param  rel_mins                    target relative minutes
    \param  qrg                         target frequency, in kHz
    \param  tcalls                      all the entrants
    \param  calls_with_no_freq_info     entrants whose logged frequency information is useless
    \param  calls_with_poor_freq_info   entrants whose logged frequency information is untrustworthy
    \param  all_qsos_this_band          all the QSOs on this band, per entrant
    \param  all_vec                     all the QSOs on this band
    \param  all_time_map                map time to index in <i>all_vec</i>
    \param  minimum_minutes             the global minimum number of minutes in the logs (typically zero)
    \param  maximum_minutes             the global maximum number of minutes in the logs (typically 2879)
    \param  ignore_call                 ignore this call in the logs (typically the call of the station that reported working <i>call</i> at this time and frequency)
    \return                             whether <i>call</i> appears to have been running at time <i>time</i> on frequency <i>qrg</i>
*/
bool is_stn_running(const string& call, const int rel_mins, const int qrg, const unordered_set<string>& tcalls, const unordered_set<string>& calls_with_no_freq_info,
                      const unordered_set<string>& calls_with_poor_freq_info, const unordered_map<string /* tcall */, vector<small_qso>>& all_qsos_this_band, const vector<small_qso>& all_vec,
                      const vector<vector<small_qso>::const_iterator>& all_time_map, const int minimum_minutes, const int maximum_minutes,
                      const string& ignore_call)
{ if (!tcalls.contains(call))        // is it a valid entrant call?
    return false;

  const int target_minutes { rel_mins };
  
  if (call_has_good_freq_info(call, calls_with_no_freq_info, calls_with_poor_freq_info))            // call has good frequency info
  { const auto [lb, ub] { get_bounds(target_minutes, minimum_minutes, maximum_minutes, CLOCK_SKEW, all_qsos_this_band.at(call)) };
   
    return ANY_OF(lb, ub, [qrg, &call] (const small_qso& qso) { return ( abs(qrg - qso.qrg()) <= FREQ_SKEW); });
  }

// can't trust call's frequency information; does someone else say that they have worked him here?
  const int lower_target_minutes { max(target_minutes - CLOCK_SKEW, minimum_minutes) };
  const int upper_target_minutes { min(target_minutes + CLOCK_SKEW, maximum_minutes) };
  
  return ANY_OF(all_time_map.at(lower_target_minutes), all_time_map.at(upper_target_minutes + 1),
                 [qrg, &call, &ignore_call] (const small_qso& qso) { return (qso.tcall() != ignore_call) and (qso.rcall() == call) and (abs(qrg - qso.qrg()) <= FREQ_SKEW); });
}

/*! \brief                      Return lower and upper bounds for a time range in a vector<small_qso>
    \param  target_minutes      the target time
    \param  minimum_minutes     minimum time in a contest (usually 0)
    \param  maximum_minutes     maximum time in a contest (usually 1439 or 2879)
    \param  ALLOWED_SKEW        maximum permitted skew time, in minutes
    \param  vec                 chronologically-ordered target vector of small_qso elements
    \return                     iterators to the lower bound and upper bound of the range, taking CLOCK_SKEW into account
*/
pair<vector<small_qso>::const_iterator, vector<small_qso>::const_iterator> get_bounds(const int target_minutes, const int minimum_minutes, const int maximum_minutes,
                                                                                      const int ALLOWED_SKEW, const vector<small_qso>& vec)
{ const int  lower_target_minutes { max(target_minutes - ALLOWED_SKEW, minimum_minutes) };
  const int  upper_target_minutes { min(target_minutes + ALLOWED_SKEW, maximum_minutes) };
  const auto lb                   { lower_bound(vec.cbegin(), vec.cend(), lower_target_minutes, [] (const auto& element, const auto& target) { return (element.rel_mins() < target); }) };
  const auto ub                   { upper_bound(lb, vec.cend(), upper_target_minutes, [] (const auto& target, const auto& element) { return (target < element.rel_mins()); }) }; 
  
  return pair { lb, ub };
}
