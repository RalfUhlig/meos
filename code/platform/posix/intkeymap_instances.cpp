/************************************************************************
    MeOS - Orienteering Software
    Linux port: explicit instantiations of intkeymap.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version. See LICENSE in the repository root.
************************************************************************/

// intkeymap's member functions are defined in intkeymapimpl.hpp, which only some
// source files include; others (oDataContainer.cpp, the inline oEvent::getNumRunners)
// call them without seeing a definition and rely on an implicit instantiation in
// another object file. With optimization and LTO, GCC inlines those instantiations
// and emits no symbol, so the link fails. Explicit instantiation definitions here
// provide the out-of-line members for the specializations used across files.
// Members are instantiated one by one: instantiating the whole class would also
// instantiate the unused const operator[], which does not compile. When an upstream
// drop needs another specialization, the link names it as an undefined
// intkeymap<...> member.

#include "StdAfx.h"

#include "intkeymapimpl.hpp"

class oClub;
class oCourse;
class oRunner;
class oTeam;

// Type names, so that const T & in the macro means a const pointer.
typedef oClub *ClubPointer;
typedef oCourse *CoursePointer;
typedef oRunner *RunnerPointer;
typedef oTeam *TeamPointer;

#define MEOS_INSTANTIATE_INTKEYMAP(T, KEY)                                      \
  template intkeymap<T, KEY>::intkeymap();                                     \
  template intkeymap<T, KEY>::intkeymap(int);                                  \
  template intkeymap<T, KEY>::intkeymap(const intkeymap &);                    \
  template const intkeymap<T, KEY> &intkeymap<T, KEY>::operator=(const intkeymap &); \
  template intkeymap<T, KEY>::~intkeymap();                                    \
  template void intkeymap<T, KEY>::clear();                                    \
  template void intkeymap<T, KEY>::insert(KEY, const T &);                     \
  template T &intkeymap<T, KEY>::get(KEY);                                     \
  template void intkeymap<T, KEY>::remove(KEY);                                \
  template bool intkeymap<T, KEY>::lookup(KEY, T &) const;                     \
  template int intkeymap<T, KEY>::size() const;                                \
  template bool intkeymap<T, KEY>::empty() const;                              \
  template void intkeymap<T, KEY>::resize(int);

MEOS_INSTANTIATE_INTKEYMAP(int, int)
MEOS_INSTANTIATE_INTKEYMAP(int, __int64)
MEOS_INSTANTIATE_INTKEYMAP(ClubPointer, int)
MEOS_INSTANTIATE_INTKEYMAP(CoursePointer, int)
MEOS_INSTANTIATE_INTKEYMAP(RunnerPointer, int)
MEOS_INSTANTIATE_INTKEYMAP(TeamPointer, int)
