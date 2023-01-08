// $Id: command_line.cpp 2 2023-01-07 18:44:13Z n7dr $

// Released under the GNU Public License, version 2
//   see: https://www.gnu.org/licenses/gpl-2.0.html

// Principal author: N7DR

// Copyright owners:
//    N7DR

/*! \file   command_line.h

    API for managing the command line
*/

#include "command_line.h"

#include <ctype.h>

using namespace std;

// -----------  command_line  ----------------

/*! \class  command_line
    \brief  Class that implements management of the command line
*/

/// internal initialisation function
void command_line::_init(void)
{ _arg = new string [_argc];

  for (unsigned int n { 0 }; n < _argc; n++)
//    _arg[n] = (string)(_argv[n]);
    _arg[n] = static_cast<string>(_argv[n]);
}

/// command_line = command_line
void command_line::operator=(const command_line& cl)
{ _argc = cl._argc;
  _argv = cl._argv;

  if (_arg)
    delete [] _arg;

  _init();
}

/*! \brief      Obtain the base name of the program
    \return     the base name of the program (i.e., with no "/" characters)
*/
string command_line::base_program_name(void) const
{ string rv { program_name() };

  if (const size_t posn { rv.find_last_of("/"s) }; posn != string::npos)
    rv = rv.substr(posn + 1);

  return rv;
}

/*! \brief      Return parameter number (wrt 1)
    \param  n   parameter number
    \return     the value of the <i>n</i>th parameter

    If the value of <i>n</i> does not correspond to a parameter that was actually present, this functions throws an x_command_line_invalid_parameter()
*/
string command_line::parameter(const unsigned int n) const
{ if (n >= _argc)
    throw x_command_line_invalid_parameter();

  return _arg[n];
}

// convert entire line to lower case
void command_line::tolower(void)
{ for (int n { 0 }; n <= n_parameters(); n++)
    tolower(n);
}

// convert item to lower case (note this is NOT the same as parameter number)
void command_line::tolower(const unsigned int n)
{ if (n >= _argc)
    throw x_command_line_invalid_parameter();

  string& s { _arg[n] };
    
  for (unsigned int i { 0 }; i < s.length(); i++)
    s[i] = _tolower(s[i]);
}

// convert entire line to original case
void command_line::tooriginal(void)
{ for (unsigned int n { 0 }; n < _argc; n++)
    tooriginal(n);
}

// convert item to original case (note this is NOT the same as parameter number)
void command_line::tooriginal(const unsigned int n)
{ if (n >= _argc)
    throw x_command_line_invalid_parameter();
    
//  _arg[n] = (std::string)(_argv[n]);
  _arg[n] = static_cast<string>(_argv[n]);
}

// convert entire line to upper case
void command_line::toupper(void)
{ for (int n { 0 }; n <= n_parameters(); n++)
    toupper(n);
}

// convert item to upper case (note this is NOT the same as parameter number)
void command_line::toupper(const unsigned int n)
{ if (n >= _argc)
    throw x_command_line_invalid_parameter();
    
  string& s { _arg[n] };
    
  for (unsigned int i { 0 }; i < s.length(); i++)
    s[i] = _toupper(s[i]);
}

// is a particular value present?
bool command_line::value_present(const string& s) const
{ bool rv { false };
  
  for (int n { 1 }; n < n_parameters(); n++)    // < because last might be the actual value
    rv |= (parameter(n) == s);
    
  return rv;
}

// return a value
string command_line::value(const string& s) const
{ if (!value_present(s))
    return string();

  string rv;

  for (int n { 1 }; n < n_parameters(); n++)    // < because last might be the actual value
  { if (parameter(n) == s)
      rv = parameter(n + 1);
  }

  return rv;
}

// is a particular parameter present?
bool command_line::parameter_present(const string& s) const
{ bool rv { false };
  
  for (int n { 1 }; n <= n_parameters(); n++)
    rv = (rv or (parameter(n) == s));

  return rv;
}
