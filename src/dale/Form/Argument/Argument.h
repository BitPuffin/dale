#ifndef DALE_FORM_ARGUMENT
#define DALE_FORM_ARGUMENT

#include "../../Units/Units.h"

namespace dale
{
bool FormArgumentParse(Units *units,
            Variable *var,
            Node *top,
            bool allow_anon_structs,
            bool allow_bitfields,
            bool allow_refs);
}

#endif
