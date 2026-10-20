/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TMT_FABLE_H
#define TMT_FABLE_H
#include <musica/micm.h>
/* Named operating points from the selected Fable reference. Apply before
 * creating any states. Each sets its own tolerances and integration controls.
 * The compatibility default remains MUSICA's three-stage Rosenbrock method. */
typedef enum { TmtFableL2 = 1, TmtFableL3r = 2, TmtFableExtrapolation = 3 } TmtFableProfile;
void TmtConfigureFable(MICM *, TmtFableProfile, Error *);
#endif
