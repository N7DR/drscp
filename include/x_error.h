// $Id: x_error.h 2 2023-01-07 18:44:13Z n7dr $

#ifndef X_ERROR_H
#define X_ERROR_H

/*! \file 	x_error.h

    A simple base error class
*/

#include <exception>
#include <string>

/*! \class  x_error
    \brief  Trivial base class for errors
*/

class x_error : public std::exception
{
protected:

  int         _code;            ///< Error code
  std::string _reason;          ///< Error reason

public:

/*! \brief      Constructor from code and reason
    \param  n   error code
    \param  s   reason
*/
  inline x_error(const int n, const std::string& s) :
    _code(n),
    _reason(s)
    { }

/*! \brief  Destructor
*/
  inline ~x_error(void) throw()
    { }

/*! \brief  RO access to _code
*/
  inline int code(void) const
    { return _code; }

/*! \brief  RO access to _reason
*/
  inline std::string reason(void) const
    { return _reason; }
};

#endif    // X_ERROR_H

