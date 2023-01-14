// $Id: count_values.h 5 2023-01-14 15:26:12Z n7dr $

// Released under the GNU Public License, version 2
//   see: https://www.gnu.org/licenses/gpl-2.0.html

// Principal author: N7DR

// Copyright owners:
//    N7DR

/*! \file   count_values.h

    Class for counting values.
*/

#ifndef COUNT_VALUES_H
#define COUNT_VALUES_H

#include "macros.h"

#include <functional>
#include <unordered_map>

// -----------------------------------------------------  count_values  ---------------------------------

/*! \class  count_values
    \brief  Count different values
*/

template <class T, class U = int32_t>
class count_values : public std::unordered_map<T, U>
{
protected:
  
public:

/// return the number of distinguishable values
  inline size_t n_values(void) const
    { return std::unordered_map<T, U>(*this).size(); }

/// return the sum total of counts
  U total_count(void) const
  { U rv { };
 
    for (const auto& [ value, n ] : std::unordered_map<T, U>(*this))
      rv += n;
     
     return rv;
  }

/// add one to a count (and create it if it is not extant)
  void operator+=(const T& v)
  { std::unordered_map<T, U>* p { this };
   
    (*p)[v]++;
  }

/// get the value and corresponding count for the largest count 
  std::pair<T, U> maximum(void) const
  { std::pair<T, U> rv;
 
    U max_value { std::numeric_limits<U>::min() };
 
    for (const auto& [ value, n ] : std::unordered_map<T, U>(*this))
    { if (n > max_value)
        rv = { value, n };
    }
   
    return rv;
  }

/// get the value and corresponding count for the least count
  std::pair<T, U> minimum(void) const
  { std::pair<T, U> rv;
 
    U min_value { std::numeric_limits<U>::max() };
 
    for (const auto& [ value, n ] : std::unordered_map<T, U>(*this))
    { if (n < min_value)
        rv = { value, n };
    }
   
    return rv;
  }

/// invert, so that the count is the key into a set of values 
  std::map<U, std::set<T>, std::greater<U>> sorted_invert(void) const
  { std::map<U, std::set<T>, std::greater<U>> rv { };
 
    for (const auto& [ value, n ] : std::unordered_map<T, U>(*this))
      rv[n] += value; 

    return rv;
  }

/// add one (or create an entry if necessary) for each of a vector of values 
  inline void operator+=(const std::vector<T>& tvec)
    { FOR_ALL(tvec, [this] (const T& v) { (*this) += v; }); }

#if 0
  template <class UnaryPredicate>   
  std::unordered_set<T> filter(UnaryPredicate pred)
  { std::unordered_set<T> rv;
  
    APPEND_IF(std::unordered_map<T, U>(*this), rv, pred);
 
    return rv;
  }
#endif
};

/// define count_value to be a kind of unordered_map
template<class T, class U>
constexpr bool is_specialization<count_values<T, U>, std::unordered_map> = true;

#endif              // COUNT_VALUES_H

