/* 
 * Telescope control daemon.
 * Copyright (C) 2003-2009 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <libnova/libnova.h>

#include "device.h"
#include "libnova_cpp.h"

#include "teld.h"
#include "clicupola.h"
#include "clirotator.h"

#include "pluto/norad.h"
#include "pluto/observe.h"

#include "telmodel.h"
#include "gpointmodel.h"
#include "tpointmodel.h"

#define OPT_BLOCK_ON_STANDBY  OPT_LOCAL + 117
#define OPT_HORIZON	   OPT_LOCAL + 118
#define OPT_CORRECTION	OPT_LOCAL + 119
#define OPT_WCS_MULTI	 OPT_LOCAL + 120
#define OPT_PARK_POS	  OPT_LOCAL + 121
#define OPT_DEC_UPPER_LIMIT   OPT_LOCAL + 122
#define OPT_RTS2_MODEL	OPT_LOCAL + 123
#define OPT_T_POINT_MODEL	 OPT_LOCAL + 124

#define EVENT_TELD_MPEC_REFRESH  RTS2_LOCAL_EVENT + 1200
#define EVENT_TRACKING_TIMER	 RTS2_LOCAL_EVENT + 1201

using namespace rts2teld;

Telescope::Telescope (int in_argc, char **in_argv, bool diffTrack, bool hasTracking, int hasUnTelCoordinates, bool hasAltAzDiff):rts2core::Device (in_argc, in_argv, DEVICE_TYPE_MOUNT, "T0")
{
	for (int i = 0; i < 4; i++)
	{
		timerclear (dir_timeouts + i);
	}

	parkPos = NULL;
	parkFlip = NULL;

	nextCupSync = 0;
	lastTrackLog = 0;

	useParkFlipping = false;
	
	decUpperLimit = NULL;

	// object
	createValue (oriRaDec, "ORI", "original position (J2000)", true, RTS2_VALUE_WRITABLE);
	// users offset
	createValue (offsRaDec, "OFFS", "object offset", true, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE, 0);
	offsRaDec->setValueRaDec (0, 0);

	if (hasAltAzDiff)
	{
		createValue (offsAltAz, "AZALOFFS", "alt-azimuth offsets", true, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE, 0);
		offsAltAz->setValueAltAz (0, 0);
	}
	else
	{
		offsAltAz = NULL;
	}

	createValue (woffsRaDec, "woffs", "offsets waiting to be applied", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE, 0);
	woffsRaDec->setValueRaDec (0, 0);
	woffsRaDec->resetValueChanged ();

	cos_lat = NAN;
	sin_lat = NAN;
	tan_lat = NAN;

	if (diffTrack)
	{
		createValue (diffTrackRaDec, "DRATE", "[deg/hour] differential tracking rate", true, RTS2_VALUE_WRITABLE | RTS2_DT_DEGREES);
		diffTrackRaDec->setValueRaDec (0, 0);

		createValue (diffTrackStart, "DSTART", "start time of differential tracking", false);
		createValue (diffTrackOn, "DON", "differential tracking on/off", false, RTS2_VALUE_WRITABLE | RTS2_DT_ONOFF);
		diffTrackOn->setValueBool (false);
	}
	else
	{
		diffTrackRaDec = NULL;
		diffTrackStart = NULL;
		diffTrackOn = NULL;
	}

	if (hasAltAzDiff)
	{
		createValue (diffTrackAltAz, "AZALRATE", "[deg/hour] differential tracking rate in alt/az", true, RTS2_VALUE_WRITABLE | RTS2_DT_DEGREES);
		diffTrackAltAz->setValueAltAz (0, 0);

		createValue (diffTrackAltAzStart, "AZALSTART", "start time of differential tracking", false);
		createValue (diffTrackAltAzOn, "AZALON", "differential tracking on/off", false, RTS2_VALUE_WRITABLE | RTS2_DT_ONOFF);
		diffTrackAltAzOn->setValueBool (false);
	}
	else
	{
		diffTrackAltAz = NULL;
		diffTrackAltAzStart = NULL;
		diffTrackAltAzOn = NULL;
	}

	if (hasTracking)
	{
		createValue (tracking, "TRACKING", "telescope tracking", true, RTS2_VALUE_WRITABLE);
		tracking->addSelVal ("off");
		tracking->addSelVal ("on");
		tracking->addSelVal ("sidereal");
		createValue (trackingInterval, "tracking_interval", "[s] interval for tracking loop", false, RTS2_VALUE_WRITABLE | RTS2_DT_TIMEINTERVAL);
		trackingInterval->setValueFloat (0.5);

		createValue (trackingFrequency, "tracking_frequency", "[Hz] tracking frequency", false);
		createValue (trackingFSize, "tracking_num", "numbers of tracking request to calculate tracking stat", false, RTS2_VALUE_WRITABLE);
		createValue (trackingWarning, "tracking_warning", "issue warning if tracking frequency drops bellow this number", false, RTS2_VALUE_WRITABLE);
		trackingWarning->setValueFloat (1);
		trackingFSize->setValueInteger (20);

		createValue (skyVect, "SKYSPD", "[deg/hour] tracking speeds vector (in RA/DEC)", true, RTS2_DT_DEGREES);
	}
	else
	{
		tracking = NULL;
		trackingInterval = NULL;
		trackingFrequency = NULL;
		trackingWarning = NULL;
		skyVect = NULL;
	}

	lastTrackingRun = NAN;

	createValue (objRaDec, "OBJ", "telescope FOV center position (J2000) - with offsets applied", true);

	tarTelRaDec = NULL;
	tarTelAltAz = NULL;

	switch (hasUnTelCoordinates)
	{
		case 1:
			createValue (tarTelRaDec, "TAR_TEL", "target position (OBJ) in telescope coordinates (DEC in 180 .. -180 range)", true);
			break;

		case -1:
			createValue (tarTelAltAz, "TAR_TEL", "target position (OBJ) in telescope coordinates (AZ in 180 .. -180 range)", true);
			break;
	}

	createValue (tarRaDec, "TAR", "target position with computed corrections (precession, refraction, aberation) applied", true);

	createValue (corrRaDec, "CORR_", "correction from closed loop", true, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE, 0);
	corrRaDec->setValueRaDec (0, 0);

	createValue (wcorrRaDec, "wcorr", "corrections which waits for being applied", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE, 0);
	wcorrRaDec->setValueRaDec (0, 0);
	wcorrRaDec->resetValueChanged ();

	createValue (total_offsets, "total_offsets", "[deg] OFFS - corr", false, RTS2_DT_DEG_DIST_180);

	createValue (wCorrImgId, "wcorr_img", "Image id waiting for correction", false, RTS2_VALUE_WRITABLE, 0);

	// position error
	createValue (posErr, "pos_err", "error in degrees", false, RTS2_DT_DEG_DIST_180);

	createValue (precessed, "precessed", "target coordinates, aberated and precessed", false);
	createValue (nutated, "nutated", "target coordinates, nutated", false);
	createValue (aberated, "aberated", "target coordinates, aberated", false);
	createValue (refraction, "refraction", "[deg] refraction (in altitude)", false, RTS2_DT_DEG_DIST_180);

	createValue (modelRaDec, "MO_RTS2", "[deg] RTS2 model offsets", true, RTS2_DT_DEGREES, 0);
	modelRaDec->setValueRaDec (0, 0);

	createValue (telTargetRaDec, "tel_target", "target RA DEC telescope coordinates - virtual coordinates feeded to TCS", false);

	wcs_crval1 = wcs_crval2 = NULL;

	// target + model + corrections = sends to tel ... TEL (read from sensors, if possible)
	createValue (telRaDec, "TEL", "mount position (from sensors, sky coordinates)", true);

	// like TEL, but raw values, no normalization (i.e. no flip transformation):
	telUnRaDec = NULL;
	telUnAltAz = NULL;
	switch (hasUnTelCoordinates)
	{
		case 1:
			createValue (telUnRaDec, "U_TEL", "mount position (from sensors, raw coordinates, no flip transformation)", true);
			break;
		case -1:
			createValue (telUnAltAz, "U_TEL", "mount position (from sensors, raw coordinates, no flip transformation)", true);
			break;
	}

	createValue (telAltAz, "TEL_", "horizontal telescope coordinates", true, RTS2_VALUE_WRITABLE);

	createValue (pointingModel, "pointing", "pointing model (equ, alt-az, ...)", false, 0, 0);
	pointingModel->addSelVal ("EQU");
	pointingModel->addSelVal ("ALT-AZ");
	pointingModel->addSelVal ("ALT-ALT");

	createValue (mpec, "mpec_target", "MPEC string (used for target calculation, if set)", false, RTS2_VALUE_WRITABLE);
	createValue (mpec_refresh, "mpec_refresh", "refresh MPEC ra_diff and dec_diff every mpec_refresh seconds", false, RTS2_VALUE_WRITABLE);
	mpec_refresh->setValueDouble (3600);

	createValue (mpec_angle, "mpec_angle", "timespan from which MPEC ra_diff and dec_diff will be computed", false, RTS2_VALUE_WRITABLE);
	mpec_angle->setValueDouble (3600);

	if (diffTrack)
	{
		createValue (diffRaDec, "mpec_dif", "[degrees/hour] calculated MPEC differential tracking", true, RTS2_DT_DEGREES, TEL_MOVING);
		diffRaDec->setValueRaDec (0, 0);
	}
	else
	{
		diffRaDec = NULL;
	}

	createValue (trackingLogInterval, "tracking_log_interval", "[s] interval for tracking logs", false, RTS2_VALUE_WRITABLE);
	trackingLogInterval->setValueDouble (30);

	createValue (tle_l1, "tle_l1_target", "TLE target line 1", false);
	createValue (tle_l2, "tle_l2_target", "TLE target line 2", false);
	createValue (tle_ephem, "tle_ephem", "TLE emphemeris type", false);
	createValue (tle_distance, "tle_distance", "[km] satellite distance", false);
	createValue (tle_freeze, "tle_freeze", "if true, stop updating TLE positions; put current speed vector to DRATE", false, RTS2_VALUE_WRITABLE);
	tle_freeze->setValueBool (false);
	createValue (tle_rho_sin_phi, "tle_rho_sin", "TLE rho_sin_phi (observatory position)", false);
	createValue (tle_rho_cos_phi, "tle_rho_cos", "TLE rho_cos_phi (observatory position)", false);
	createValue (tle_refresh, "tle_refresh", "refresh TLE ra_diff and dec_diff every tle_refresh seconds", false, RTS2_VALUE_WRITABLE);

	createConstValue (telLatitude, "LATITUDE", "observatory latitude", true, RTS2_DT_DEGREES);
	createConstValue (telLongitude, "LONGITUD", "observatory longitude", true, RTS2_DT_DEGREES);
	createConstValue (telAltitude, "ALTITUDE", "observatory altitude", true);
	createConstValue (telPressure, "PRESSURE", "observatory atmospheric pressure", false, RTS2_VALUE_WRITABLE);
	telPressure->setValueFloat (1000);

	createValue (refreshIdle, "refresh_idle", "idle and tracking refresh interval", false, RTS2_DT_TIMEINTERVAL | RTS2_VALUE_WRITABLE);
	createValue (refreshSlew, "refresh_slew", "slew refresh interval", false, RTS2_DT_TIMEINTERVAL | RTS2_VALUE_WRITABLE);

	refreshIdle->setValueDouble (60);
	refreshSlew->setValueDouble (0.5);

	createValue (mountParkTime, "PARKTIME", "Time of last mount park");

	createValue (blockMove, "block_move", "if true, any software movement of the telescope is blocked", false, RTS2_VALUE_WRITABLE);
	blockMove->setValueBool (false);

	createValue (blockOnStandby, "block_on_standby", "Block telescope movement if switched to standby/off mode. Enable it if switched back to on.", false, RTS2_VALUE_WRITABLE);
	blockOnStandby->setValueBool (false);

	createValue (airmass, "AIRMASS", "Airmass of target location");
	createValue (hourAngle, "HA", "Location hour angle", true, RTS2_DT_RA);
	createValue (lst, "LST", "Local Sidereal Time", true, RTS2_DT_RA);
	createValue (jdVal, "JD", "Modified Julian Date", true);

	createValue (targetDistance, "target_distance", "distance to the target in degrees", false, RTS2_DT_DEG_DIST);
	createValue (targetStarted, "move_started", "time when movement was started", false);
	createValue (targetReached, "move_end", "expected time when telescope will reach the destination", false);

	targetDistance->setValueDouble (NAN);
	targetStarted->setValueDouble (NAN);
	targetReached->setValueDouble (NAN);

	createValue (moveNum, "MOVE_NUM", "number of movements performed by the driver; used in corrections for synchronization", true);
	moveNum->setValueInteger (0);

	createValue (corrImgId, "CORR_IMG", "ID of last image used for correction", true, RTS2_VALUE_WRITABLE);
	corrImgId->setValueInteger (0);

	createValue (failedMoveNum, "move_failed", "number of failed movements", false);
	failedMoveNum->setValueInteger (0);

	createValue (lastFailedMove, "move_failed_last", "last time movement failed", false);

	defaultRotang = 0;
	createValue (rotang, "MNT_ROTA", "mount rotang", true, RTS2_DT_WCS_ROTANG | RTS2_VALUE_WRITABLE);

	move_connection = NULL;

	createValue (ignoreCorrection, "ignore_correction", "corrections below this value will be ignored", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	ignoreCorrection->setValueDouble (0);

	createValue (defIgnoreCorrection, "def_ignore_cor", "default ignore correction. ignore_correction is changed to this value at the end of the script", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	defIgnoreCorrection->setValueDouble (0);

	createValue (smallCorrection, "small_correction", "correction bellow this value will be considered as small", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	smallCorrection->setValueDouble (0);

	createValue (correctionLimit, "correction_limit", "corrections above this limit will be ignored", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	correctionLimit->setValueDouble (5.0);

	createValue (modelLimit, "model_limit", "model separation limit", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	modelLimit->setValueDouble (5.0);

	createValue (telFov, "telescope_fov", "telescope field of view", false, RTS2_DT_DEG_DIST_180 | RTS2_VALUE_WRITABLE);
	telFov->setValueDouble (180.0);

	createValue (telFlip, "MNT_FLIP", "telescope flip");
	createValue (peekFlip, "peek_flip", "predicted telescope flip for peek command", false);

	flip_move_start = 0;
	flip_longest_path = -1;

	// default is to aply model corrections
	createValue (calPrecession, "CAL_PREC", "if precession is included in target calculations", false, RTS2_VALUE_WRITABLE);
	calPrecession->setValueBool (false);

	createValue (calNutation, "CAL_NUT", "if nutation is included in target calculations", false, RTS2_VALUE_WRITABLE);
	calNutation->setValueBool (false);

	createValue (calAberation, "CAL_ABER", "if aberation is included in target calculations", false, RTS2_VALUE_WRITABLE);
	calAberation->setValueBool (false);

	createValue (calRefraction, "CAL_REFR", "if refraction is included in target calculations", false, RTS2_VALUE_WRITABLE);
	calRefraction->setValueBool (false);

	createValue (calModel, "CAL_MODE", "if model calculations are included in target calcuations", false, RTS2_VALUE_WRITABLE);
	calModel->setValueBool (false);

	createValue (cupolas, "cupolas", "Cupola(s) connected to telescope. They should sync as telescope moves.", false);

	createValue (rotators, "rotators", "Rotator(s) connected to telescope.", false);

	tPointModelFile = NULL;
	rts2ModelFile = NULL;
	model = NULL;

	createValue (standbyPark, "standby_park", "Park telescope when switching to standby", false, RTS2_VALUE_WRITABLE);
	standbyPark->addSelVal ("no");
	standbyPark->addSelVal ("daytime only");
	standbyPark->addSelVal ("nightime");

	standbyPark->setValueInteger (0);

	horizonFile = NULL;
	hardHorizon = NULL;

	wcs_multi = '!';

	addOption (OPT_RTS2_MODEL, "rts2-model", 1, "RTS2 pointing model filename");
	addOption (OPT_T_POINT_MODEL, "t-point-model", 1, "T-Point model filename");
	addOption ('l', NULL, 1, "separation limit (corrections above that number in degrees will be ignored)");
	addOption ('g', NULL, 1, "minimal good separation. Correction above that number will be aplied immediately. Default to 180 deg");

	addOption ('c', NULL, 1, "minimal value for corrections. Corrections bellow that value will be rejected.");

	addOption ('s', NULL, 1, "park when switched to standby - 1 only at day, 2 even at night");
	addOption (OPT_BLOCK_ON_STANDBY, "block-on-standby", 0, "block telescope movement when switching to standby");

	addOption ('r', NULL, 1, "telescope rotang");
	addOption (OPT_HORIZON, "horizon", 1, "telescope hard horizon");
	addOption (OPT_CORRECTION, "max-correction", 1, "correction limit (in arcsec)");
	addOption (OPT_WCS_MULTI, "wcs-multi", 1, "letter for multiple WCS (A-Z,-)");
	addOption (OPT_DEC_UPPER_LIMIT, "dec-upper-limit", 1, "maximal declination the telescope is able to point to");

	setIdleInfoInterval (refreshIdle->getValueDouble ());

	moveInfoCount = 0;
	moveInfoMax = 100;
}

Telescope::~Telescope (void)
{
	delete model;
}

double Telescope::getLocSidTime (double JD)
{
	double ret;
	ret = ln_get_apparent_sidereal_time (JD) * 15.0 + telLongitude->getValueDouble ();
	return ln_range_degrees (ret) / 15.0;
}

void Telescope::setTarTelRaDec (struct ln_equ_posn *pos)
{
	if (tarTelRaDec == NULL)
		return;

	tarTelRaDec->setValueRaDec (pos->ra, pos->dec);
}

void Telescope::setTarTelAltAz (struct ln_hrz_posn *hrz)
{
	if (tarTelAltAz == NULL)
		return;

	tarTelAltAz->setValueAltAz (hrz->alt, hrz->az);
}

int Telescope::calculateTarget (double JD, struct ln_equ_posn *out_tar, int32_t &ac, int32_t &dc, bool writeValues, double haMargin, bool forceShortest)
{
	double tar_distance = NAN;

	switch (tracking->getValueInteger ())
	{
		case 1:	   // on object
			// calculate from MPEC..
			if (mpec->getValueString ().length () > 0)
			{
				struct ln_lnlat_posn observer;
				struct ln_equ_posn parallax;
				observer.lat = telLatitude->getValueDouble ();
				observer.lng = telLongitude->getValueDouble ();
				LibnovaCurrentFromOrbit (out_tar, &mpec_orbit, &observer, telAltitude->getValueDouble (), JD, &parallax);
                                if (writeValues)
					setOrigin (out_tar->ra, out_tar->dec);
				break;
			}
			// calculate from TLE..
			else if (tle_freeze->getValueBool () == false && tle_l1->getValueString ().length () > 0 && tle_l2->getValueString ().length () > 0)
			{
				calculateTLE (JD, out_tar->ra, out_tar->dec, tar_distance);
				out_tar->ra = ln_rad_to_deg (out_tar->ra);
				out_tar->dec = ln_rad_to_deg (out_tar->dec);
				if (writeValues)
					setOrigin (out_tar->ra, out_tar->dec);
				break;
			}
			// don't break, as sidereal tracking will be used
		default:
			// get from ORI, if constant..
			getOrigin (out_tar);
			if (!isnan (diffTrackStart->getValueDouble ()))
				addDiffRaDec (out_tar, (JD - diffTrackStart->getValueDouble ()) * 86400.0);
	}

	// offsets, corrections,..
	out_tar->ra += getOffsetRa ();
	out_tar->dec += getOffsetDec ();

	// crossed pole, put on opposite side..
	if (out_tar->dec > 90)
	{
		out_tar->ra = ln_range_degrees (out_tar->ra + 180.0);
		out_tar->dec = 180 - out_tar->dec;
	}
	else if (out_tar->dec < -90)
	{
		out_tar->ra = ln_range_degrees (out_tar->ra + 180.0);
		out_tar->dec = -180 - out_tar->dec;
	}

	if (writeValues)
		setObject (out_tar->ra, out_tar->dec);

	return sky2counts (JD, out_tar, ac, dc, writeValues, haMargin, forceShortest);
}

int Telescope::calculateTracking (double JD, double sec_step, int32_t &ac, int32_t &dc, int32_t &ac_speed, int32_t &dc_speed)
{
	struct ln_equ_posn eqpos, t_eqpos;
	// refresh current target..

	int32_t t_ac = ac;
	int32_t t_dc = dc;
	int ret = calculateTarget (JD, &eqpos, t_ac, t_dc, true, 0, true);
	if (ret)
		return ret;

	ret = calculateTarget (JD + sec_step / 86400.0, &t_eqpos, t_ac, t_dc, false, 0, true);
	if (ret)
		return ret;

	// for speed vector calculation..
	double ra_diff = t_eqpos.ra - eqpos.ra;
	double dec_diff = t_eqpos.dec - eqpos.dec;

	if (ra_diff > 180.0)
		ra_diff = ra_diff - 360;
	else if (ra_diff < -180.0)
		ra_diff = ra_diff + 360;

	skyVect->setValueRaDec (3600 * ra_diff / sec_step, 3600 * dec_diff / sec_step);

	ac_speed = (ac - t_ac) / sec_step;
	dc_speed = (dc - t_dc) / sec_step;

	ac = t_ac;
	dc = t_dc;

	return 0;
}

int Telescope::sky2counts (double JD, struct ln_equ_posn *pos, int32_t &ac, int32_t &dc, bool writeValues, double haMargin, bool forceShortest)
{
	return -1;
}

void Telescope::addDiffRaDec (struct ln_equ_posn *tar, double secdiff)
{
	if (diffTrackRaDec && diffTrackOn->getValueBool () == true)
	{
		tar->ra += getDiffTrackRa () * secdiff / 3600.0;
		tar->dec += getDiffTrackDec () * secdiff / 3600.0;
	}
}

void Telescope::addDiffAltAz (struct ln_hrz_posn *hrz, double secdiff)
{
	if (diffTrackAltAz && diffTrackAltAzOn->getValueBool () == true)
	{
		hrz->az += getDiffTrackAz () * secdiff / 3600.0;
		hrz->alt += getDiffTrackAlt () * secdiff / 3600.0;
	}
}

void Telescope::createRaPAN ()
{
	createValue (raPAN, "ra_pan", "RA panning", false, RTS2_VALUE_WRITABLE);
	raPAN->addSelVal ("NONE");
	raPAN->addSelVal ("MINUS");
	raPAN->addSelVal ("PLUS");

	createValue (raPANSpeed, "ra_pan_speed", "speed of RA panning", false, RTS2_VALUE_WRITABLE);
}

void Telescope::createDecPAN ()
{
	createValue (decPAN, "dec_pan", "DEC panning", false, RTS2_VALUE_WRITABLE);
	decPAN->addSelVal ("NONE");
	decPAN->addSelVal ("MINUS");
	decPAN->addSelVal ("PLUS");

	createValue (decPANSpeed, "dec_pan_speed", "speed of DEC panning", false, RTS2_VALUE_WRITABLE);
}



int Telescope::processOption (int in_opt)
{
	switch (in_opt)
	{
		case OPT_RTS2_MODEL:
			rts2ModelFile = optarg;
			calModel->setValueBool (true);
			break;
		case OPT_T_POINT_MODEL:
			tPointModelFile = optarg;
			calModel->setValueBool (true);
			break;
		case 'l':
			modelLimit->setValueDouble (atof (optarg));
			break;
		case 'g':
			telFov->setValueDouble (atof (optarg));
			break;
		case 'c':
			defIgnoreCorrection->setValueCharArr (optarg);
			ignoreCorrection->setValueDouble (defIgnoreCorrection->getValueDouble ());
			break;
		case OPT_BLOCK_ON_STANDBY:
			blockOnStandby->setValueBool (true);
			break;
		case 's':
			standbyPark->setValueCharArr (optarg);
			break;
		case 'r':
			defaultRotang = atof (optarg);
			rotang->setValueDouble (defaultRotang);
			break;
		case OPT_HORIZON:
			horizonFile = optarg;
			break;
		case OPT_CORRECTION:
			correctionLimit->setValueCharArr (optarg);
			break;
		case OPT_WCS_MULTI:
			if (strlen (optarg) != 1 || ! (optarg[0] == '-' || (optarg[0] >= 'A' || optarg[0] <= 'Z')))
			{
				std::cerr << "invalid --wcs-multi option " << optarg << std::endl;
				return -1;
			}
			if (optarg[0] == '-')
				wcs_multi = '\0';
			else
				wcs_multi = optarg[0];
			break;
		case OPT_DEC_UPPER_LIMIT:
			createValue (decUpperLimit, "dec_upper_limit", "upper limit on telescope declination", false, RTS2_VALUE_WRITABLE);
			decUpperLimit->setValueCharArr (optarg);
			break;

		case OPT_PARK_POS:
			{
				std::istringstream *is;
				is = new std::istringstream (std::string(optarg));
				double palt,paz;
				int flip;
				char c;
				*is >> palt >> c >> paz;
				if (is->fail () || c != ':')
				{
					logStream (MESSAGE_ERROR) << "Cannot parse alt-az park position " << optarg << sendLog;
					delete is;
					return -1;
				}
				*is >> c >> flip;
				if (is->fail ())
					flip = -1;

				delete is;
				if (parkPos == NULL)
				{
					createParkPos (palt, paz, flip);
				}
				else
				{
					parkPos->setValueAltAz (palt, paz);
					parkFlip->setValueInteger (flip);
				}
			}
			break;
		default:
			return rts2core::Device::processOption (in_opt);
	}
	return 0;
}

void Telescope::calculateCorrAltAz ()
{
	struct ln_equ_posn equ_target;
	struct ln_equ_posn equ_corr;

	struct ln_lnlat_posn observer;

	double jd = ln_get_julian_from_sys ();

	equ_target.ra = tarRaDec->getRa ();
	equ_target.dec = tarRaDec->getDec ();

	equ_corr.ra = equ_target.ra;
	equ_corr.dec = equ_target.dec;

	applyCorrRaDec (&equ_corr, true, true);

	observer.lng = telLongitude->getValueDouble ();
	observer.lat = telLatitude->getValueDouble ();

	double ast = ln_get_apparent_sidereal_time(jd);

	ln_get_hrz_from_equ_sidereal_time (&equ_target, &observer, ast, &tarAltAz);

	if (corrRaDec->getRa () == 0 && corrRaDec->getDec () == 0)
	{
		corrAltAz.alt = tarAltAz.alt;
		corrAltAz.az = tarAltAz.az;
	}
	else
	{
		ln_get_hrz_from_equ_sidereal_time (&equ_corr, &observer, ast, &corrAltAz);
	}
}

double Telescope::getCorrZd ()
{
	if (corrRaDec->getRa () == 0 && corrRaDec->getDec () == 0)
		return 0;

	calculateCorrAltAz ();

	return corrAltAz.alt - tarAltAz.alt;
}

double Telescope::getCorrAz ()
{
	if (corrRaDec->getRa () == 0 && corrRaDec->getDec () == 0)
		return 0;

	calculateCorrAltAz ();

	return tarAltAz.az - corrAltAz.az;
}

double Telescope::getTargetDistance ()
{
	struct ln_equ_posn tar,tel;
	if (tarTelAltAz != NULL)
	{
		struct ln_hrz_posn hrz_tar, hrz_tel;
		tarTelAltAz->getAltAz (&hrz_tar);	
		getTelAltAz (&hrz_tel);

		tar.ra = hrz_tar.az;
		tar.dec = hrz_tar.alt;
		tel.ra = hrz_tel.az;
		tel.dec = hrz_tel.alt;
	}
	else
	{
		getTelTargetRaDec (&tar);
		getTelRaDec (&tel);
		normalizeRaDec (tel.ra, tel.dec);
	}

	if (isnan(tar.ra) || isnan(tar.dec) || isnan(tel.ra) || isnan(tel.dec))
		return -1;

	return ln_get_angular_separation (&tel, &tar);
}

void Telescope::getTargetAltAz (struct ln_hrz_posn *hrz, double jd)
{
	struct ln_equ_posn tar;
	getTarget (&tar);
	struct ln_lnlat_posn observer;
	observer.lng = telLongitude->getValueDouble ();
	observer.lat = telLatitude->getValueDouble ();
	ln_get_hrz_from_equ_sidereal_time (&tar, &observer, ln_get_apparent_sidereal_time (jd), hrz);
}

double Telescope::getTargetHa ()
{
	return getTargetHa (ln_get_julian_from_sys ());
}

double Telescope::getTargetHa (double jd)
{
	return ln_range_degrees (ln_get_apparent_sidereal_time (jd) - tarRaDec->getRa ());
}

double Telescope::getLstDeg (double JD)
{
	return ln_range_degrees (15. * ln_get_apparent_sidereal_time (JD) + telLongitude->getValueDouble ());
}

int Telescope::setValue (rts2core::Value * old_value, rts2core::Value * new_value)
{
	if (old_value == tracking)
	{
		if (((rts2core::ValueBool *) new_value)->getValueBool ())
		{
			// we don't allow starting of tracking via setValue in these cases...
			if (blockMove->getValueBool () == true || (getState () & TEL_MASK_MOVING) == TEL_PARKING || (getState () & TEL_MASK_MOVING) == TEL_PARKED)
				return -2;
		}
		if (setTracking (new_value->getValueInteger (), true, false))
			return -2;
	}
	else if (old_value == mpec)
	{
		std::string desc;
		if (LibnovaEllFromMPC (&mpec_orbit, desc, new_value->getValue ()))
			if (LibnovaEllFromMPCComet (&mpec_orbit, desc, new_value->getValue ()))
				return -2;
	}
	else if (old_value == diffTrackRaDec)
	{
	  	setDiffTrack (((rts2core::ValueRaDec *)new_value)->getRa (), ((rts2core::ValueRaDec *)new_value)->getDec ());
	}
	else if (old_value == diffTrackOn)
	{
		logStream (MESSAGE_INFO) << "switching DON to " << ((rts2core::ValueBool *) new_value)->getValueBool () << sendLog;
		if (((rts2core::ValueBool *) new_value)->getValueBool () == true)
		{
			setDiffTrack (getDiffTrackRa (), getDiffTrackDec ());
		}
		else
		{
			setDiffTrack (0, 0);
		}
	}
	else if (old_value == diffTrackAltAz)
	{
		setDiffTrackAltAz (((rts2core::ValueAltAz *)new_value)->getAz (), ((rts2core::ValueAltAz *)new_value)->getAlt ());
	}
	else if (old_value == diffTrackAltAzOn)
	{
		logStream (MESSAGE_INFO) << "switching AZALON to " << ((rts2core::ValueBool *) new_value)->getValueBool () << sendLog;
		if (((rts2core::ValueBool *) new_value)->getValueBool () == true)
		{
			setDiffTrackAltAz (getDiffTrackAz (), getDiffTrackAlt ());
		}
		else
		{
			setDiffTrackAltAz (0, 0);
		}
	}
	else if (old_value == refreshIdle)
	{
		switch (getState () & TEL_MASK_MOVING)
		{
			case TEL_OBSERVING:
			case TEL_PARKED:
				setIdleInfoInterval (((rts2core::ValueDouble *)new_value)->getValueDouble ());
				break;
		}
	}
	else if (old_value == refreshSlew)
	{
		switch (getState () & TEL_MASK_MOVING)
		{
			case TEL_MOVING:
			case TEL_PARKING:
				setIdleInfoInterval (((rts2core::ValueDouble *)new_value)->getValueDouble ());
				break;
		}
	}
	else if (old_value == tle_freeze)
	{
		if (tle_l1->getValueString ().length () == 0 || tle_l2->getValueString ().length () == 0)
			return -2;

		if (((rts2core::ValueBool *) new_value)->getValueBool () == false)
		{
			// reset DRATE, probably set by tle_freeze = true
			setDiffTrack (0, 0);
			setDiffTrackAltAz (0, 0);
		}
		else
		{
			// TLE not yet calculated..
			double JD = ln_get_julian_from_sys ();
			struct ln_equ_posn p1, p2, speed;
			double d1, d2;
			const double sec_step = 10.0;
			calculateTLE (JD, p1.ra, p1.dec, d1);
			calculateTLE (JD + sec_step / 86400.0, p2.ra, p2.dec, d2);
			speed.ra = (3600 * ln_rad_to_deg (p2.ra - p1.ra)) / sec_step;
			if (speed.ra > 180.0)
				speed.ra -= 360.0;
			else if (speed.ra < -180.0)
				speed.ra += 360.0;
			speed.dec = (3600 * ln_rad_to_deg (p2.dec - p1.dec)) / sec_step;
			diffTrackRaDec->setValueRaDec (speed.ra, speed.dec);
			sendValueAll (diffTrackRaDec);
			setDiffTrack (speed.ra, speed.dec);
		}
	}
	return rts2core::Device::setValue (old_value, new_value);
}

void Telescope::valueChanged (rts2core::Value * changed_value)
{
	if (changed_value == woffsRaDec)
	{
		maskState (BOP_EXPOSURE, BOP_EXPOSURE, "blocking exposure for offsets");
	}
	if (changed_value == oriRaDec)
	{
		mpec->setValueString ("");
		startResyncMove (NULL, 0);
	}
	if (changed_value == mpec)
	{
		recalculateMpecDIffs ();
		startResyncMove (NULL, 0);
	}
	if (changed_value == offsRaDec || changed_value == corrRaDec)
	{
		startOffseting (changed_value);
	}
	if (changed_value == telAltAz)
	{
		moveAltAz ();
	}
	rts2core::Device::valueChanged (changed_value);
}

void Telescope::applyAberation (struct ln_equ_posn *pos, double JD, bool writeValue)
{
	ln_get_equ_aber (pos, JD, pos);
	if (writeValue)
		aberated->setValueRaDec (pos->ra, pos->dec);
}

void Telescope::applyNutation (struct ln_equ_posn *pos, double JD, bool writeValue)
{
	struct ln_nutation nut;
	ln_get_nutation (JD, &nut);

	double rad_ecliptic = ln_deg_to_rad (nut.ecliptic + nut.obliquity);
	double sin_ecliptic = sin (rad_ecliptic);

	double rad_ra = ln_deg_to_rad (pos->ra);
	double rad_dec = ln_deg_to_rad (pos->dec);

	double sin_ra = sin (rad_ra);
	double cos_ra = cos (rad_ra);

	double tan_dec = tan (rad_dec);

	double d_ra = (cos (rad_ecliptic) + sin_ecliptic * sin_ra * tan_dec) * nut.longitude - cos_ra * tan_dec * nut.obliquity;
	double d_dec = (sin_ecliptic * cos_ra) * nut.longitude + sin_ra * nut.obliquity;

	pos->ra += d_ra;
	pos->dec += d_dec;

	if (writeValue)
		nutated->setValueRaDec (pos->ra, pos->dec);
}

void Telescope::applyPrecession (struct ln_equ_posn *pos, double JD, bool writeValue)
{
	ln_get_equ_prec (pos, JD, pos);
	if (writeValue)
		precessed->setValueRaDec (pos->ra, pos->dec);
}

void Telescope::applyRefraction (struct ln_equ_posn *pos, double JD, bool writeValue)
{
	struct ln_hrz_posn hrz;
	struct ln_lnlat_posn obs;
	double ref;

	obs.lng = telLongitude->getValueDouble ();
	obs.lat = telLatitude->getValueDouble ();

	double ast = ln_get_apparent_sidereal_time(JD);

	ln_get_hrz_from_equ_sidereal_time (pos, &obs, ast, &hrz);
	ref = ln_get_refraction_adj (hrz.alt, getPressure (), 10);
	hrz.alt += ref;
	if (writeValue)
		refraction->setValueDouble (ref);
	ln_get_equ_from_hrz (&hrz, &obs, JD, pos);
}

void Telescope::afterMovementStart ()
{
	nextCupSync = 0;
	startCupolaSync ();
}

void Telescope::incMoveNum ()
{
	if (diffTrackRaDec)
	{
		diffTrackRaDec->setValueRaDec (0, 0);
		diffTrackStart->setValueDouble (NAN);
	  	setDiffTrack (0,0);
		diffTrackOn->setValueBool (false);
	}

	if (diffTrackAltAz)
	{
		diffTrackAltAz->setValueAltAz (0, 0);
		diffTrackStart->setValueDouble (NAN);
		setDiffTrackAltAz (0, 0);
		diffTrackAltAzOn->setValueBool (false);
	}

	tle_freeze->setValueBool (false);

	// reset offsets
	offsRaDec->setValueRaDec (0, 0);
	offsRaDec->resetValueChanged ();

	if (offsAltAz)
	{
		offsAltAz->setValueAltAz (0, 0);
		offsAltAz->resetValueChanged ();
	}

	woffsRaDec->setValueRaDec (0, 0);
	woffsRaDec->resetValueChanged ();

	modelRaDec->setValueRaDec (0, 0);
	modelRaDec->resetValueChanged ();

	corrRaDec->setValueRaDec (0, 0);
	corrRaDec->resetValueChanged ();

	wcorrRaDec->setValueRaDec (0, 0);
	wcorrRaDec->resetValueChanged ();

	moveNum->inc ();

	ignoreCorrection->setValueDouble (defIgnoreCorrection->getValueDouble ());

	corrImgId->setValueInteger (0);
	wCorrImgId->setValueInteger (0);
}

void Telescope::recalculateMpecDIffs ()
{
	if (mpec->getValueString ().length () <= 0)
		return;

	double mp_diff = mpec_angle->getValueDouble () / 86400.0;

	double JD = ln_get_julian_from_sys () - 1.5 * mp_diff;

	// get MPEC positions..
	struct ln_equ_posn pos[4];

	struct ln_lnlat_posn observer;
	struct ln_equ_posn parallax;
	observer.lat = telLatitude->getValueDouble ();
	observer.lng = telLongitude->getValueDouble ();

	int i;

	for (i = 0; i < 4; i++)
	{
		LibnovaCurrentFromOrbit (pos + i, &mpec_orbit, &observer, telAltitude->getValueDouble (), JD, &parallax);
		JD += mp_diff;
	}

	struct ln_equ_posn pos_diffs[3];

	for (i = 0; i < 3; i++)
	{
		pos_diffs[i].ra = pos[i].ra - pos[i+1].ra;
		pos_diffs[i].dec = pos[i].dec - pos[i+1].dec;
	}

	if (diffRaDec)
	{
		diffRaDec->setValueRaDec (pos_diffs[1].ra, pos_diffs[1].dec);
	}
}

int Telescope::applyCorrRaDec (struct ln_equ_posn *pos, bool invertRa, bool invertDec)
{
  	struct ln_equ_posn pos2;
	int decCoeff = 1;

	// to take care of situation when *pos holds raw mount coordinates
	if (fabs (pos->dec) > 90.0)
		decCoeff = -1;

	if (invertRa)
		pos2.ra = ln_range_degrees (pos->ra - corrRaDec->getRa ());
	else
		pos2.ra = ln_range_degrees (pos->ra + corrRaDec->getRa ());

	if (invertDec)
		pos2.dec = pos->dec - decCoeff * corrRaDec->getDec ();
	else
		pos2.dec = pos->dec + decCoeff * corrRaDec->getDec ();
	if (ln_get_angular_separation (&pos2, pos) > correctionLimit->getValueDouble ())
	{
		logStream (MESSAGE_WARNING) << "correction " << LibnovaDegDist (corrRaDec->getRa ()) << " " << LibnovaDegDist (corrRaDec->getDec ()) << " is above limit, ignoring it" << sendLog;
		return -1; 
	}
	pos->ra = pos2.ra;
	pos->dec = pos2.dec;
	return 0;
}

void Telescope::applyModel (struct ln_equ_posn *m_pos, struct ln_equ_posn *tt_pos, struct ln_equ_posn *model_change, double JD)
{
	computeModel (m_pos, model_change, JD);

	modelRaDec->setValueRaDec (model_change->ra, model_change->dec);
        tt_pos->ra -= model_change->ra;
        tt_pos->dec -= model_change->dec;

	// also include corrRaDec correction to get resulting values...
	if (applyCorrRaDec (tt_pos) == 0)
	{
		model_change->ra -= corrRaDec->getRa();
		if (fabs (m_pos->dec) > 90.0)
			model_change->dec += corrRaDec->getDec();
		else
			model_change->dec -= corrRaDec->getDec();
	}
}

void Telescope::applyModelAltAz (struct ln_hrz_posn *hrz, struct ln_hrz_posn *err)
{
	if (!model || calModel->getValueBool () == false)
		return;
	model->getErrAltAz (hrz, err);
	modelRaDec->setValueRaDec (err->alt, err->az);
}

void Telescope::applyModelPrecomputed (struct ln_equ_posn *pos, struct ln_equ_posn *model_change, bool applyCorr)
{
	modelRaDec->setValueRaDec (model_change->ra, model_change->dec);

	// also include corrRaDec correction to get resulting values when applyCorr set to true...
	if (applyCorr == true && applyCorrRaDec (pos) == 0)
	{
		model_change->ra -= corrRaDec->getRa();
		if (fabs (pos->dec) > 90.0)
			model_change->dec += corrRaDec->getDec();
		else
			model_change->dec -= corrRaDec->getDec();
	}
}

void Telescope::computeModel (struct ln_equ_posn *pos, struct ln_equ_posn *model_change, double JD)
{
	struct ln_equ_posn hadec;
	double ra;
	double ls;

	if (!model || calModel->getValueBool () == false)
	{
		model_change->ra = 0;
		model_change->dec = 0;
		return;
	}
	ls = getLstDeg (JD);
	hadec.ra = ls - pos->ra;	// intentionally without ln_range_degrees
	hadec.dec = pos->dec;

	model->reverse (&hadec);

	// get back from model - from HA
	ra = ls - hadec.ra;

	// calculate change
	model_change->ra = ln_range_degrees (pos->ra - ra);	// here must be ln_range_degrees, flip computation above might have destroyed the continuity
	if (model_change->ra > 180.0)
		model_change->ra -= 360.0;
	model_change->dec = pos->dec - hadec.dec;

	hadec.ra = ra;

	double sep = ln_get_angular_separation (&hadec, pos);

	// change above modelLimit are strange - reject them
	if (sep > modelLimit->getValueDouble ())
	{
		logStream (MESSAGE_WARNING)
			<< "telescope computeModel big change - rejecting "
			<< model_change->ra << " "
			<< model_change->dec << " sep " << sep << sendLog;
		model_change->ra = 0;
		model_change->dec = 0;
		return;
	}

	if (getDebug ())
	{
		logStream (MESSAGE_DEBUG)
			<< "Telescope::computeModel offsets ra: "
			<< model_change->ra << " dec: " << model_change->dec
			<< sendLog;
	}
}

int Telescope::init ()
{
	int ret;
	ret = rts2core::Device::init ();
	if (ret)
		return ret;

	if (wcs_multi != '!')
	{
		createValue (wcs_crval1, multiWCS ("CRVAL1", wcs_multi), "first reference value", false, RTS2_DT_WCS_CRVAL1);
		createValue (wcs_crval2, multiWCS ("CRVAL2", wcs_multi), "second reference value", false, RTS2_DT_WCS_CRVAL2);
	}

	if (tPointModelFile && rts2ModelFile)
	{
		std::cerr << "cannot specify both 'tpoint-model' and 'rts2-model'. Please choose only one model file" << std::endl;
		return -1;
	}

	if (rts2ModelFile)
	{
		model = new rts2telmodel::GPointModel (getLatitude ());
		ret = model->load (rts2ModelFile);
		if (ret)
			return ret;
		rts2core::DoubleArray *p;
		createValue (p, "rts2_model", "RTS2 model parameters", false);
		for (int i = 0; i < 9; i++)
			p->addValue (((rts2telmodel::GPointModel *) model)->params[i]);

	}
	else if (tPointModelFile)
	{
		model = new rts2telmodel::TPointModel (getLatitude ());
		ret = model->load (tPointModelFile);
		if (ret)
			return ret;
		for (std::vector <rts2telmodel::TPointModelTerm *>::iterator iter = ((rts2telmodel::TPointModel*) model)->begin (); iter != ((rts2telmodel::TPointModel*) model)->end (); iter++)
			addValue (*iter, TEL_MOVING);
	}

	if (horizonFile)
	{
		hardHorizon = new ObjectCheck (horizonFile);
	}

	return 0;
}

int Telescope::initValues ()
{
	int ret;
	ret = info ();
	if (ret)
		return ret;
	tarRaDec->setFromValue (telRaDec);
	oriRaDec->setFromValue (telRaDec);
	objRaDec->setFromValue (telRaDec);
	aberated->setFromValue (telRaDec);
	precessed->setFromValue (telRaDec);
	refraction->setValueDouble (NAN);
	modelRaDec->setValueRaDec (0, 0);
	telTargetRaDec->setFromValue (telRaDec);

	if (wcs_crval1 && wcs_crval2)
	{
		wcs_crval1->setValueDouble (objRaDec->getRa ());
		wcs_crval2->setValueDouble (objRaDec->getDec ());
	}

	// calculate rho_sin_phi, ...
	double r_s, r_c;
	lat_alt_to_parallax (ln_deg_to_rad (getLatitude ()), getAltitude (), &r_c, &r_s);

	tle_rho_cos_phi->setValueDouble (r_c);
	tle_rho_sin_phi->setValueDouble (r_s);

	return rts2core::Device::initValues ();
}

void Telescope::checkMoves ()
{
	int ret;
	if ((getState () & TEL_MASK_MOVING) == TEL_MOVING)
	{
		ret = isMoving ();
		if (ret >= 0)
		{
			setTimeout (ret);
			if (moveInfoCount == moveInfoMax)
			{
				info ();
				if (move_connection)
					sendInfo (move_connection);
				moveInfoCount = 0;
			}
			else
			{
				moveInfoCount++;
			}
		}
		else if (ret == -1)
		{
			failedMove ();
			if (move_connection)
				sendInfo (move_connection);
			maskState (DEVICE_ERROR_MASK | TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE, DEVICE_ERROR_HW | TEL_NOT_CORRECTING | TEL_OBSERVING, "move finished with error");
			move_connection = NULL;
			setIdleInfoInterval (refreshIdle->getValueDouble ());
		}
		else if (ret == -2)
		{
			infoAll ();
			ret = endMove ();
			if (ret)
			{
				failedMove ();

				maskState (DEVICE_ERROR_MASK | TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE, DEVICE_ERROR_HW | TEL_NOT_CORRECTING | TEL_OBSERVING, "move finished with error");
			}
			else
			{
				maskState (TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE, TEL_NOT_CORRECTING | TEL_OBSERVING, "move finished without error");
			}

			if (move_connection)
			{
				sendInfo (move_connection);
			}
			move_connection = NULL;
			setIdleInfoInterval (refreshIdle->getValueDouble ());
		}
	}
	else if ((getState () & TEL_MASK_MOVING) == TEL_PARKING)
	{
		ret = isParking ();
		if (ret >= 0)
		{
			setTimeout (ret);
		}
		if (ret == -1)
		{
			failedMove ();

			useParkFlipping = false;
			infoAll ();
			maskState (DEVICE_ERROR_MASK | TEL_MASK_MOVING | BOP_EXPOSURE, DEVICE_ERROR_HW | TEL_PARKED, "park command finished with error");
			setIdleInfoInterval (refreshIdle->getValueDouble ());
		}
		else if (ret == -2)
		{
			useParkFlipping = false;
			infoAll ();
			logStream (MESSAGE_INFO) << "parking of telescope finished" << sendLog;
			ret = endPark ();
			if (ret)
			{
				failedMove ();

				maskState (DEVICE_ERROR_MASK | TEL_MASK_MOVING | BOP_EXPOSURE, DEVICE_ERROR_HW | TEL_PARKED, "park command finished with error");
			}
			else
			{
				maskState (TEL_MASK_MOVING | BOP_EXPOSURE, TEL_PARKED, "park command finished without error");
			}
			if (move_connection)
			{
				sendInfo (move_connection);
			}
			setIdleInfoInterval (refreshIdle->getValueDouble ());
		}
	}
}

int Telescope::idle ()
{
	checkMoves ();
	return rts2core::Device::idle ();
}

void Telescope::postEvent (rts2core::Event * event)
{
	switch (event->getType ())
	{
		case EVENT_TRACKING_TIMER:
			// if tracking is still relevant, reschedule
			if (isTracking ())
			{
				runTracking ();
				addTimer (trackingInterval->getValueFloat (), event);
				return;
			}
			// don't call stopMove - it shall be called if tracking was stop by stopTracking..
			break;
		case EVENT_CUP_SYNCED:
			maskState (TEL_MASK_CUP, TEL_NO_WAIT_CUP);
			break;
		case EVENT_CUP_ENDED:
			logStream (MESSAGE_INFO) << "removed cupola " << ((ClientCupola *)(event->getArg ()))->getName () << sendLog;
			cupolas->remove (((ClientCupola *)(event->getArg ()))->getName ());
			break;
		case EVENT_ROT_ENDED:
			logStream (MESSAGE_INFO) << "removed rotator " << ((ClientRotator *)(event->getArg ()))->getName () << sendLog;
			rotators->remove (((ClientRotator *)(event->getArg ()))->getName ());
			break;
	}
	rts2core::Device::postEvent (event);
}

int Telescope::willConnect (rts2core::NetworkAddress * in_addr)
{
	if (in_addr->getType () == DEVICE_TYPE_CUPOLA || in_addr->getType () == DEVICE_TYPE_ROTATOR)
		return 1;
	return rts2core::Device::willConnect (in_addr);
}

rts2core::DevClient *Telescope::createOtherType (rts2core::Connection * conn, int other_device_type)
{
	switch (other_device_type)
	{
		case DEVICE_TYPE_CUPOLA:
			logStream (MESSAGE_INFO) << "connecting to cupula " << conn->getName () << sendLog;
			cupolas->addValue (std::string (conn->getName ()));
			return new ClientCupola (conn);
		case DEVICE_TYPE_ROTATOR:
			logStream (MESSAGE_INFO) << "connecting to rotator " << conn->getName () << sendLog;
			rotators->addValue (std::string (conn->getName ()));
			conn->setSendAll (false);
			return new ClientRotator (conn);
	}
	return rts2core::Device::createOtherType (conn, other_device_type);
}

void Telescope::changeMasterState (rts2_status_t old_state, rts2_status_t new_state)
{
	// stop when STOP is triggered
	if ((getState () & STOP_MASK) != STOP_EVERYTHING && (new_state & STOP_MASK) == STOP_EVERYTHING)
	{
		blockMove->setValueBool (true);
		sendValueAll (blockMove);
		maskState (TEL_MASK_TRACK, TEL_NOTRACK, "tracking blocked");
		stopMove ();
		logStream (MESSAGE_WARNING) << "stoped movement of the delescope, triggered by STOP_MASK change" << sendLog;
	}
	// enable movement when stop state was cleared
	else if ((old_state & STOP_MASK) == STOP_EVERYTHING && (new_state & STOP_MASK) != STOP_EVERYTHING)
	{
		blockMove->setValueBool (false);
		sendValueAll (blockMove);
		logStream (MESSAGE_DEBUG) << "reenabled movement as all stop states were cleared" << sendLog;
	}

	// park us during day..
	if ((((new_state & SERVERD_STATUS_MASK) == SERVERD_DAY)
		|| (new_state & SERVERD_ONOFF_MASK)) && standbyPark->getValueInteger () != 0)
	{
		// ignore nighttime park request
		if (standbyPark->getValueInteger () == 2 || ! ((new_state & SERVERD_STATUS_MASK) == SERVERD_NIGHT || (new_state & SERVERD_STATUS_MASK) == SERVERD_DUSK || (new_state & SERVERD_STATUS_MASK) == SERVERD_DAWN))
		{
	  		if ((getState () & TEL_MASK_MOVING) != TEL_PARKED && (getState () & TEL_MASK_MOVING) != TEL_PARKING)
				startPark (NULL);
		}
	}

	if (blockOnStandby->getValueBool () == true)
	{
		if (new_state & SERVERD_ONOFF_MASK)
			blockMove->setValueBool (true);
		// only set blockMove to false if switching from off/standy
		else if (old_state & SERVERD_ONOFF_MASK)
			blockMove->setValueBool (false);
	}

	rts2core::Device::changeMasterState (old_state, new_state);
}

void Telescope::setTelLongLat (double longitude, double latitude)
{
	telLongitude->setValueDouble (longitude);
	telLatitude->setValueDouble (latitude);

	cos_lat = cos (ln_deg_to_rad (latitude));
	sin_lat = sin (ln_deg_to_rad (latitude));
	tan_lat = sin_lat / cos_lat;
}

void Telescope::setTelAltitude (float altitude)
{
	telAltitude->setValueDouble (altitude);
	telPressure->setValueFloat (getAltitudePressure (altitude, 1010));
}

void Telescope::getTelAltAz (struct ln_hrz_posn *hrz)
{
	struct ln_equ_posn telpos;
	struct ln_lnlat_posn observer;

	telpos.ra = telRaDec->getRa ();
	telpos.dec = telRaDec->getDec ();

	normalizeRaDec (telpos.ra, telpos.dec);

	observer.lng = telLongitude->getValueDouble ();
	observer.lat = telLatitude->getValueDouble ();

	ln_get_hrz_from_equ_sidereal_time (&telpos, &observer, ln_get_apparent_sidereal_time (ln_get_julian_from_sys ()), hrz);
}

double Telescope::estimateTargetTime ()
{
	// most of mounts move at 2 degrees per second.
	return getTargetDistance () / 2.0;
}

int Telescope::setTracking (int track, bool addTrackingTimer, bool send)
{
	if (tracking != NULL)
	{
		tracking->setValueInteger (track);
		// make sure we will not run two timers
		deleteTimers (EVENT_TRACKING_TIMER);
		if (track > 0)
		{
			maskState (TEL_MASK_TRACK, TEL_TRACKING, "tracking started");
			if (addTrackingTimer == true)
			{
				runTracking ();
				addTimer (trackingInterval->getValueFloat (), new rts2core::Event (EVENT_TRACKING_TIMER));
			}
		}
		else
		{
			stopTracking ();
		}
		if (send == true)
			sendValueAll (tracking);
	}
	return 0;
}

void Telescope::startTracking (bool check)
{
	if (tracking == NULL)
		return;

	if (check && tracking->getValueInteger () == 0)
	{
		tracking->setValueInteger (1);
		sendValueAll (tracking);
	}
	trackingFrequency->clearStat ();
	setTracking (tracking->getValueInteger (), true);
}

void Telescope::stopTracking (const char *msg)
{
	lastTrackingRun = NAN;
	stopMove ();
	maskState (TEL_MASK_TRACK, TEL_NOTRACK, msg);
}

void Telescope::runTracking ()
{
	double n = getNow ();
	if (lastTrackLog < n)
	{
		logTracking ();
		lastTrackLog = n + trackingLogInterval->getValueDouble ();
	}
	startCupolaSync ();
}

void Telescope::logTracking ()
{
	rts2core::LogStream ls = logStream (MESSAGE_DEBUG | DEBUG_MOUNT_TRACKING_LOG);
		// 1                            2
	ls	<< tarRaDec->getRa () << " " << tarRaDec->getDec () << " "
		// 3                          4
		<< precessed->getRa () << " " << precessed->getDec () << " "
		// 5                           6
		<< nutated->getRa () << " " << nutated->getDec () << " "
		// 7                            8
		<< aberated->getRa () << " " << aberated->getDec () << " "
		// 9
		<< refraction->getValueDouble () << " "
		// 10                             11 
		<< modelRaDec->getRa () << " " << modelRaDec->getDec () << " ";
	if (tarTelRaDec != NULL)
		//    12                              13
		ls << tarTelRaDec->getRa () << " " << tarTelRaDec->getDec () << " ";
	else
		ls << "-0 -0 ";

	if (tarTelAltAz != NULL)
		//    14                              15
		ls << tarTelAltAz->getAz () << " " << tarTelAltAz->getAlt ();
	else
		ls << "-0 -0 ";
	ls << sendLog;
}

void Telescope::updateTrackingFrequency ()
{
	double n = getNow ();
	if (!isnan (lastTrackingRun) && n != lastTrackingRun)
	{
		double tv = 1 / (n - lastTrackingRun);
		trackingFrequency->addValue (tv, trackingFSize->getValueInteger ());
		trackingFrequency->calculate ();
		if (tv < trackingWarning->getValueFloat ())
			logStream (MESSAGE_ERROR) << "lost telescope tracking, frequency is " << tv << sendLog;
	}
	lastTrackingRun = n;
}

void Telescope::calculateTLE (double JD, double &ra, double &dec, double &dist_to_satellite)
{
	double sat_params[N_SAT_PARAMS], observer_loc[3];
	double t_since;
	double sat_pos[3]; /* Satellite position vector */

	observer_cartesian_coords (JD, ln_deg_to_rad (getLongitude ()), tle_rho_cos_phi->getValueDouble (), tle_rho_sin_phi->getValueDouble (), observer_loc);

	t_since = (JD - tle.epoch) * 1440.;
	switch (tle_ephem->getValueInteger ())
	{
		case 0:
			SGP_init (sat_params, &tle);
			SGP (t_since, &tle, sat_params, sat_pos, NULL);
			break;
		case 1:
			SGP4_init (sat_params, &tle);
			SGP4 (t_since, &tle, sat_params, sat_pos, NULL);
			break;
		case 2:
			SGP8_init (sat_params, &tle);
			SGP8 (t_since, &tle, sat_params, sat_pos, NULL);
			break;
		case 3:
			SDP4_init (sat_params, &tle);
			SDP4 (t_since, &tle, sat_params, sat_pos, NULL);
			break;
		case 4:
			SDP8_init (sat_params, &tle);
			SDP8 (t_since, &tle, sat_params, sat_pos, NULL);
			break;
		default:
			logStream (MESSAGE_ERROR) << "invalid tle_ephem " << tle_ephem->getValueInteger () << sendLog;
	}
	get_satellite_ra_dec_delta (observer_loc, sat_pos, &ra, &dec, &dist_to_satellite);
}

void Telescope::setDiffTrack (double dra, double ddec)
{
	if (dra == 0 && ddec == 0)
	{
		diffTrackStart->setValueDouble (NAN);
		diffTrackOn->setValueBool (false);
	}
	else
	{
		diffTrackStart->setValueDouble (ln_get_julian_from_sys ());
		diffTrackOn->setValueBool (true);
	}
	sendValueAll (diffTrackStart);
	sendValueAll (diffTrackOn);
}

void Telescope::setDiffTrackAltAz (double daz, double dalt)
{
	if (daz == 0 && dalt == 0)
	{
		diffTrackAltAzStart->setValueDouble (NAN);
		diffTrackAltAzOn->setValueBool (false);
	}
	else
	{
		diffTrackAltAzStart->setValueDouble (ln_get_julian_from_sys ());
		diffTrackAltAzOn->setValueBool (true);
	}
	sendValueAll (diffTrackAltAzStart);
	sendValueAll (diffTrackAltAzOn);
}

void Telescope::addParkPosOption ()
{
	addOption (OPT_PARK_POS, "park-position", 1, "parking position (alt:az[:flip])");
}

void Telescope::createParkPos (double alt, double az, int flip)
{
	createValue (parkPos, "park_position", "mount park position", false);
	createValue (parkFlip, "park_flip", "mount parked flip strategy", false);
	parkPos->setValueAltAz (alt, az);
	parkFlip->setValueInteger (flip);
}

void Telescope::getHrzFromEqu (struct ln_equ_posn *pos, double JD, struct ln_hrz_posn *hrz)
{
	struct ln_lnlat_posn obs;
	obs.lat = getLatitude ();
	obs.lng = getLongitude ();

	ln_get_hrz_from_equ_sidereal_time (pos, &obs, ln_get_apparent_sidereal_time(JD), hrz);
}

void Telescope::getHrzFromEquST (struct ln_equ_posn *pos, double ST, struct ln_hrz_posn *hrz)
{
	struct ln_lnlat_posn obs;
	obs.lat = getLatitude ();
	obs.lng = getLongitude ();

	ln_get_hrz_from_equ_sidereal_time (pos, &obs, ST, hrz);
}

void Telescope::getEquFromHrz (struct ln_hrz_posn *hrz, double JD, struct ln_equ_posn *pos)
{
	struct ln_lnlat_posn obs;
	obs.lat = getLatitude ();
	obs.lng = getLongitude ();

	ln_get_equ_from_hrz (hrz, &obs, JD, pos);
}

double Telescope::getAltitudePressure (double alt, double see_pres)
{
	return see_pres * pow (1 - (0.0065 * alt) / 288.15, (9.80665 * 0.0289644) / (8.31447 * 0.0065));
}

int Telescope::info ()
{
	return infoJD (ln_get_julian_from_sys ());
}

int Telescope::infoJD (double JD)
{
	return infoJDLST (JD, getLstDeg (JD));
}

int Telescope::infoLST (double telLST)
{
	return infoJDLST (ln_get_julian_from_sys (), telLST);
}

int Telescope::infoJDLST (double JD, double telLST)
{
	struct ln_hrz_posn hrz;
	// calculate alt+az
	getTelAltAz (&hrz);
	telAltAz->setValueAltAz (hrz.alt, hrz.az);

	if (telFlip->getValueInteger ())
		rotang->setValueDouble (ln_range_degrees (defaultRotang + 180.0));
	else
		rotang->setValueDouble (defaultRotang);

	// fill in airmass, ha and lst
	airmass->setValueDouble (ln_get_airmass (telAltAz->getAlt (), 750));
	jdVal->setValueDouble (JD);
	lst->setValueDouble (telLST);
	hourAngle->setValueDouble (ln_range_degrees (lst->getValueDouble () - telRaDec->getRa ()));
	targetDistance->setValueDouble (getTargetDistance ());

	// check if we aren't bellow hard horizon - if yes, stop tracking..
	if (hardHorizon)
	{
		struct ln_hrz_posn hrpos;
		hrpos.alt = telAltAz->getAlt ();
		hrpos.az = telAltAz->getAz ();
		if (!hardHorizon->is_good (&hrpos))
		{
			int ret = abortMoveTracking ();
			if (ret <= 0)
				logStream (MESSAGE_ERROR) << "info retrieved below horizon position, stop move. alt az: " << hrpos.alt << " " << hrpos.az << sendLog;
		}
		else
		{
			// we are at good position, above horizon
			telescopeAboveHorizon ();
		}
	}
	
	return rts2core::Device::info ();
}

int Telescope::scriptEnds ()
{
	corrImgId->setValueInteger (0);
	woffsRaDec->setValueRaDec (0, 0);
	tracking->setValueInteger (1);

	tle_freeze->setValueBool (false);

	return rts2core::Device::scriptEnds ();
}

void Telescope::applyCorrections (struct ln_equ_posn *pos, double JD, bool writeValues)
{
	// apply all posible corrections
	if (calPrecession->getValueBool () == true)
		applyPrecession (pos, JD, writeValues);
	if (calNutation->getValueBool () == true)
		applyNutation (pos, JD, writeValues);
	if (calAberation->getValueBool () == true)
		applyAberation (pos, JD, writeValues);
	if (calRefraction->getValueBool () == true)
		applyRefraction (pos, JD, writeValues);
}

void Telescope::applyCorrections (double &tar_ra, double &tar_dec, bool writeValues)
{
	struct ln_equ_posn pos;
	pos.ra = tar_ra;
	pos.dec = tar_dec;

	applyCorrections (&pos, ln_get_julian_from_sys (), writeValues);

	tar_ra = pos.ra;
	tar_dec = pos.dec;
}

void Telescope::startCupolaSync ()
{
	double n = getNow ();
	if (nextCupSync > n)
		return;
	struct ln_equ_posn tar;
	getTarget (&tar);
	rts2core::CommandCupolaSyncTel cmd (this, tar.ra, tar.dec);
	queueCommandForType (DEVICE_TYPE_CUPOLA, cmd, NULL, true);
	nextCupSync = n + 20;
}

int Telescope::endMove ()
{
	startTracking ();
	LibnovaRaDec l_to (telRaDec->getRa (), telRaDec->getDec ());
	LibnovaRaDec l_tar (tarRaDec->getRa (), tarRaDec->getDec ());
	LibnovaRaDec l_telTar (telTargetRaDec->getRa (), telTargetRaDec->getDec ());

	logStream (INFO_MOUNT_SLEW_END | MESSAGE_INFO) << telRaDec->getRa () << " " << telRaDec->getDec () << " " << tarRaDec->getRa () << " " << tarRaDec->getDec () << " " << telTargetRaDec->getRa () << " " << telTargetRaDec->getDec () << sendLog;
	return 0;
}

void Telescope::failedMove ()
{
	failedMoveNum->inc ();
	lastFailedMove->setNow ();
}

int Telescope::abortMoveTracking ()
{
	stopTracking ("tracking below horizon");
	return 0;
}

int Telescope::startResyncMove (rts2core::Connection * conn, int correction)
{
	int ret;

	struct ln_equ_posn pos;

	// apply computed corrections (precession, aberation, refraction)
	double JD = ln_get_julian_from_sys ();

	// calculate from MPEC..
	if (mpec->getValueString ().length () > 0)
	{
		struct ln_lnlat_posn observer;
		struct ln_equ_posn parallax;
		observer.lat = telLatitude->getValueDouble ();
		observer.lng = telLongitude->getValueDouble ();
		LibnovaCurrentFromOrbit (&pos, &mpec_orbit, &observer, telAltitude->getValueDouble (), ln_get_julian_from_sys (), &parallax);

		oriRaDec->setValueRaDec (pos.ra, pos.dec);
	}

	// calculate from TLE..
	if (tle_freeze->getValueBool () == false && tle_l1->getValueString ().length () > 0 && tle_l2->getValueString ().length () > 0)
	{
		double ra, dec, dist_to_satellite;
		calculateTLE (JD, ra, dec, dist_to_satellite);
		oriRaDec->setValueRaDec (ln_rad_to_deg (ra), ln_rad_to_deg (dec));
		tle_distance->setValueDouble (dist_to_satellite);
	}

	// if object was not specified, do not move
	if (isnan (oriRaDec->getRa ()) || isnan (oriRaDec->getDec ()))
	{
		logStream (MESSAGE_ERROR) << "cannot move, null RA or DEC " << oriRaDec->getRa () << " " << oriRaDec->getDec () << sendLog;
		return -1;
	}

	// MPEC objects changes coordinates regularly, so we will not bother incrementing 
	if (mpec->getValueString ().length () > 0)
	{
		if (mpec->wasChanged ())
			incMoveNum ();
	}
	// same for TLEs
	else if (tle_l1->getValueString ().length () > 0)
	{
		if (tle_l1->wasChanged () || tle_l2->wasChanged ())
			incMoveNum ();
	}
	// object changed from last call to startResyncMove and it is a stationary object
	else if (oriRaDec->wasChanged ())
	{
		incMoveNum ();
	}

	// if some value is waiting to be applied..
	else if (wcorrRaDec->wasChanged ())
	{
		corrRaDec->incValueRaDec (wcorrRaDec->getRa (), wcorrRaDec->getDec ());
		corrImgId->setValueInteger (wCorrImgId->getValueInteger ());
	}

	if (woffsRaDec->wasChanged ())
	{
		offsRaDec->setValueRaDec (woffsRaDec->getRa (), woffsRaDec->getDec ());
	}

	// update total_offsets
	total_offsets->setValueRaDec (offsRaDec->getRa () - corrRaDec->getRa (), offsRaDec->getDec () - corrRaDec->getDec ());
	sendValueAll (total_offsets);

	LibnovaRaDec l_obj (oriRaDec->getRa (), oriRaDec->getDec ());

	// first apply offset
	applyOffsets (&pos);
	pos.ra = ln_range_degrees (pos.ra);

	objRaDec->setValueRaDec (pos.ra, pos.dec);
	sendValueAll (objRaDec);

	setCRVAL (pos.ra, pos.dec);

	// apply computed corrections (precession, aberation, refraction)
	applyCorrections (&pos, JD, true);

	LibnovaRaDec syncTo (&pos);
	LibnovaRaDec syncFrom (telRaDec->getRa (), telRaDec->getDec ());

	// now we have target position, which can be feeded to telescope
	tarRaDec->setValueRaDec (pos.ra, pos.dec);
	// calculate target after corrections from astrometry
	applyCorrRaDec (&pos, false, false);

	modelRaDec->setValueRaDec (0, 0);

	moveInfoCount = 0;

	// The following sequence is:
	// - check block_move (don't touch anything if block_move set)
	// - check TEL_PARKING (when true, return with error, we don't interrupt parking process)
	// - check TEL_MOVING and stop when needed
	// - check hardHorizon (when not fulfiled, stop with error, change state to observing)
	// - maskState TEL_MOVING | BOP_EXPOSURE
	// - startResync ()
	// This way the exposure blocking state is set ASAP and all resulting states are corresponging with real mount states (mount stopped, when needed etc).

	if (blockMove->getValueBool () == true)
	{
		logStream (MESSAGE_ERROR) << "telescope move blocked" << sendLog;
		if (conn)
			conn->sendCommandEnd (DEVDEM_E_HW, "telescope move blocked");
		return -1;
	}

	if ((getState () & TEL_MASK_MOVING) == TEL_PARKING)
	{
		logStream (MESSAGE_ERROR) << "telescope cannot move during parking" << sendLog;
		if (conn)
			conn->sendCommandEnd (DEVDEM_E_HW, "telescope move blocked");
		return -1;
	}

	if (((getState () & TEL_MASK_MOVING) == TEL_MOVING) || ((getState () & TEL_MASK_CORRECTING) == TEL_CORRECTING))
	{
		ret = stopMove ();
		if (ret)
		{
			logStream (MESSAGE_ERROR) << "failure calling stopMove before starting actual movement" << sendLog;
			return ret;
		}
	}

	struct ln_hrz_posn hrztar;
	getTargetAltAz (&hrztar, JD);
	
	if (hardHorizon)
	{
		if (!hardHorizon->is_good (&hrztar))
		{
			useParkFlipping = false;
			logStream (MESSAGE_ERROR) << "target is below hard horizon: alt az " << LibnovaHrz (&hrztar) << sendLog;
			oriRaDec->setValueRaDec (NAN, NAN);
			objRaDec->setValueRaDec (NAN, NAN);
			telTargetRaDec->setValueRaDec (NAN, NAN);
			stopMove ();
			maskState (TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE | TEL_MASK_TRACK, TEL_NOT_CORRECTING | TEL_OBSERVING | TEL_NOTRACK, "cannot perform move");
			if (conn)
				conn->sendCommandEnd (DEVDEM_E_HW, "unaccesible target");
			return -1;
		}
	}

	//check if decupperlimit option exists -if yes, apply declination constraints
	if (decUpperLimit && !isnan (decUpperLimit->getValueFloat ()))
	{
		if ((telLatitude->getValueDouble () > 0 && pos.dec > decUpperLimit->getValueFloat ())
				|| (telLatitude->getValueDouble () < 0 && pos.dec < decUpperLimit->getValueFloat ()))
		{
			useParkFlipping = false;
			logStream (MESSAGE_ERROR) << "target declination is outside of allowed values, is " << pos.dec << " limit is " << decUpperLimit->getValueFloat () << sendLog;
			oriRaDec->setValueRaDec (NAN, NAN);
			objRaDec->setValueRaDec (NAN, NAN);
			telTargetRaDec->setValueRaDec (NAN, NAN);
			stopMove ();
			maskState (TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE | TEL_MASK_TRACK, TEL_NOT_CORRECTING | TEL_OBSERVING | TEL_NOTRACK, "cannot perform move");
			if (conn)
				conn->sendCommandEnd (DEVDEM_E_HW, "declination limit violated");
			return -1;
		}
	}

	// all checks done, give proper info and set TEL_ state and BOP
	if (correction)
	{
		LibnovaDegDist c_ra (corrRaDec->getRa ());
		LibnovaDegDist c_dec (corrRaDec->getDec ());

		const char *cortype = "offseting and correcting";
	
		switch (correction)
		{
			case 1:
				cortype = "offseting";
				break;
			case 2:
				cortype = "correcting";
				break;
		}

		logStream (MESSAGE_INFO) << cortype << " to " << syncTo << " from " << syncFrom << " distances " << c_ra << " " << c_dec << sendLog;

		maskState (TEL_MASK_CORRECTING | TEL_MASK_MOVING | TEL_MASK_NEED_STOP | BOP_EXPOSURE, TEL_CORRECTING | TEL_MOVING | BOP_EXPOSURE, "correction move started");
	}
	else
	{
		if (getState () & TEL_MASK_CORRECTING)
		{
			maskState (TEL_MASK_MOVING | TEL_MASK_CORRECTING, 0, "correcting interrupted by move");
		}

		struct ln_hrz_posn hrz;
		getTelAltAz (&hrz);

		logStream (INFO_MOUNT_SLEW_START | MESSAGE_INFO) << telRaDec->getRa () << " " << telRaDec->getDec () << " " << pos.ra << " " << pos.dec << " " << hrz.az << " " << hrz.alt << " " << hrztar.az << " " << hrztar.alt << sendLog;
		maskState (TEL_MASK_MOVING | TEL_MASK_CORRECTING | TEL_MASK_NEED_STOP | TEL_MASK_TRACK | BOP_EXPOSURE, TEL_MOVING | BOP_EXPOSURE, "move started");
		flip_move_start = telFlip->getValueInteger ();
	}

	// everything is OK and prepared, let's move!
	ret = startResync ();
	if (ret)
	{
		useParkFlipping = false;
		maskState (TEL_MASK_CORRECTING | TEL_MASK_MOVING | BOP_EXPOSURE | TEL_MASK_TRACK, TEL_NOT_CORRECTING | TEL_OBSERVING, "movement failed");
		if (conn)
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot move to location");
		return ret;
	}

	targetStarted->setNow ();
	targetReached->setValueDouble (targetStarted->getValueDouble () + estimateTargetTime ());

	infoAll ();

	// synchronize cupola...
	afterMovementStart ();

	tarRaDec->resetValueChanged ();
	oriRaDec->resetValueChanged ();
	offsRaDec->resetValueChanged ();
	corrRaDec->resetValueChanged ();
	telTargetRaDec->resetValueChanged ();
	mpec->resetValueChanged ();
	tle_l1->resetValueChanged ();
	tle_l2->resetValueChanged ();

	if (woffsRaDec->wasChanged ())
	{
		woffsRaDec->resetValueChanged ();
	}

	if (wcorrRaDec->wasChanged ())
	{
		wcorrRaDec->setValueRaDec (0, 0);
		wcorrRaDec->resetValueChanged ();
	}

	move_connection = conn;

	setIdleInfoInterval (refreshSlew->getValueDouble ());

	return ret;
}

int Telescope::setTo (rts2core::Connection * conn, double set_ra, double set_dec)
{
	int ret;
	ret = setTo (set_ra, set_dec);
	if (ret)
		conn->sendCommandEnd (DEVDEM_E_HW, "cannot set to");
	sendValueAll (telRaDec);
	return ret;
}

int Telescope::setToPark (rts2core::Connection * conn)
{
	int ret;
	ret = setToPark ();
	if (ret)
		conn->sendCommandEnd (DEVDEM_E_HW, "cannot set to park");
	sendValueAll (telRaDec);
	return ret;
}

int Telescope::startPark (rts2core::Connection * conn)
{
	if (blockMove->getValueBool () == true)
	{
		logStream (MESSAGE_ERROR) << "Telescope parking blocked" << sendLog;
		return -1;
	}
	int ret;
	resetMpecTLE ();
	maskState (TEL_MASK_TRACK, TEL_NOTRACK, "stop tracking while telescope is being parked");
	if ((getState () & TEL_MASK_MOVING) == TEL_MOVING)
	{
		ret = stopMove ();
		if (ret)
		{
			logStream (MESSAGE_ERROR) << "failure calling stopMove before starting parking" << sendLog;
			return ret;
		}
	}
	useParkFlipping = true;
	if (tarTelAltAz && parkPos)
		tarTelAltAz->setFromValue (parkPos);
	stopTracking ("parking telescope");
	ret = startPark ();
	if (ret < 0)
	{
		useParkFlipping = false;
		if (conn)
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot park");
		logStream (MESSAGE_ERROR) << "parking failed" << sendLog;
	}
	else
	{
	  	logStream (MESSAGE_INFO) << "parking telescope" << sendLog;
		if (ret == 0)
		{
			offsRaDec->resetValueChanged ();
			corrRaDec->resetValueChanged ();
		}

		incMoveNum ();
		setParkTimeNow ();
		maskState (TEL_MASK_MOVING | TEL_MASK_TRACK | TEL_MASK_NEED_STOP, TEL_PARKING, "parking started");
	}

	setIdleInfoInterval (refreshSlew->getValueDouble ());

	return ret;
}

int Telescope::getFlip ()
{
	int ret;
	ret = info ();
	if (ret)
		return ret;
	return telFlip->getValueInteger ();
}

void Telescope::signaledHUP ()
{
	int ret;
	if (tPointModelFile)
	{
		delete model;
		model = new rts2telmodel::TPointModel (getLatitude ());
		ret = model->load (tPointModelFile);
		if (ret)
		{
			logStream (MESSAGE_ERROR) << "Failed to reload model from file " << tPointModelFile << sendLog;
			delete model;
			model = NULL;
		}
		else
		{
			logStream (MESSAGE_INFO) << "Reloaded model from file " << tPointModelFile << sendLog;
		}
	}
	if (rts2ModelFile)
	{
		delete model;
		model = new rts2telmodel::GPointModel (getLatitude ());
		ret = model->load (rts2ModelFile);
		if (ret)
		{
			logStream (MESSAGE_ERROR) << "Failed to reload model from file " << rts2ModelFile << sendLog;
			delete model;
			model = NULL;
		}
		else
		{
			logStream (MESSAGE_INFO) << "Reloaded model from file " << rts2ModelFile << sendLog;
		}
	}

	rts2core::Device::signaledHUP ();
}

void Telescope::startOffseting (rts2core::Value *changed_value)
{
	startResyncMove (NULL, 0);
}

int Telescope::peek (double ra, double dec)
{
	return 0;
}

int Telescope::moveAltAz ()
{
	struct ln_hrz_posn hrz;
	telAltAz->getAltAz (&hrz);
	struct ln_lnlat_posn observer;
	observer.lng = telLongitude->getValueDouble ();
	observer.lat = telLatitude->getValueDouble ();
	struct ln_equ_posn target;

	ln_get_equ_from_hrz (&hrz, &observer, ln_get_julian_from_sys (), &target);

	oriRaDec->setValueRaDec (target.ra, target.dec);
	return startResyncMove (NULL, 0);
}

int Telescope::commandAuthorized (rts2core::Connection * conn)
{
	double obj_ra;
	double obj_ha;
	double obj_dec;

	int ret;

	if (conn->isCommand (COMMAND_TELD_MOVE))
	{
		if (conn->paramNextHMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		modelOn ();
		oriRaDec->setValueRaDec (obj_ra, obj_dec);
		resetMpecTLE ();
		startTracking (true);
		ret = startResyncMove (conn, 0);
		if (ret)
			maskState (TEL_MASK_TRACK, TEL_NOTRACK, "stop tracking, move cannot be perfomed");
		return ret;
	}
	else if (conn->isCommand (COMMAND_TELD_HADEC))
	{
		if (conn->paramNextHMS (&obj_ha) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		double JD = ln_get_julian_from_sys ();
		obj_ra = getLocSidTime ( JD) * 15. - obj_ha ;

		oriRaDec->setValueRaDec (obj_ra, obj_dec);
		resetMpecTLE ();
		tarRaDec->setValueRaDec (NAN, NAN);
		return startResyncMove (conn, 0);
	}
	else if (conn->isCommand ("move_not_model"))
	{
		if (conn->paramNextHMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		modelOff ();
		oriRaDec->setValueRaDec (obj_ra, obj_dec);
		resetMpecTLE ();
		startTracking (true);
		ret = startResyncMove (conn, 0);
		if (ret)
			maskState (TEL_MASK_TRACK, TEL_NOTRACK, "stop tracking, move cannot be perfomed");
		return ret;
	}
	else if (conn->isCommand (COMMAND_TELD_MOVE_MPEC))
	{
		char *str;
		if (conn->paramNextString (&str) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		modelOn ();
		mpec->setValueString (str);
		std::string desc;
		if (LibnovaEllFromMPC (&mpec_orbit, desc, str))
			if (LibnovaEllFromMPCComet (&mpec_orbit, desc, str))
				return DEVDEM_E_PARAMSVAL;
		tle_l1->setValueString ("");
		tle_l2->setValueString ("");
		startTracking (true);
		ret = startResyncMove (conn, 0);
		if (ret)
			maskState (TEL_MASK_TRACK, TEL_NOTRACK, "stop tracking, move cannot be perfomed");
		return ret;
	}
	else if (conn->isCommand (COMMAND_TELD_PEEK))
	{
		if (conn->paramNextDMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		return peek (obj_ra, obj_dec) == 0 ? DEVDEM_OK : DEVDEM_E_PARAMSVAL;
	}
	else if (conn->isCommand (COMMAND_TELD_ALTAZ))
	{
		if (conn->paramNextDMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		telAltAz->setValueAltAz (obj_ra, obj_dec);
		resetMpecTLE ();
		maskState (TEL_MASK_TRACK, TEL_NOTRACK, "stop tracking while in AltAz");
		return moveAltAz () == 0 ? DEVDEM_OK : DEVDEM_E_PARAMSVAL;
	}
	else if (conn->isCommand ("resync"))
	{
		if (conn->paramNextHMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		oriRaDec->setValueRaDec (obj_ra, obj_dec);
		resetMpecTLE ();
		return startResyncMove (conn, 0);
	}
	else if (conn->isCommand ("setto"))
	{
		if (conn->paramNextHMS (&obj_ra) || conn->paramNextDMS (&obj_dec) || !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		modelOn ();
		return setTo (conn, obj_ra, obj_dec);
	}
	else if (conn->isCommand ("settopark"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		return setToPark (conn);
	}
	else if (conn->isCommand ("synccorr"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		ret = setTo (conn, tarRaDec->getRa (), tarRaDec->getDec ());
		if (ret)
			return DEVDEM_E_PARAMSVAL;
		corrRaDec->setValueRaDec (0, 0);
		wcorrRaDec->setValueRaDec (0, 0);
		return ret;
	}
	else if (conn->isCommand ("correct"))
	{
		int cor_mark;
		int corr_img;
		int img_id;
		double total_cor_ra;
		double total_cor_dec;
		double pos_err;
		if (conn->paramNextInteger (&cor_mark)
			|| conn->paramNextInteger (&corr_img)
			|| conn->paramNextInteger (&img_id)
			|| conn->paramNextDouble (&total_cor_ra)
			|| conn->paramNextDouble (&total_cor_dec)
			|| conn->paramNextDouble (&pos_err)
			|| !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		if (applyCorrectionsFixed (total_cor_ra, total_cor_dec) == 0)
			return DEVDEM_OK;
		if (cor_mark == moveNum->getValueInteger () && corr_img == corrImgId->getValueInteger () && img_id > wCorrImgId->getValueInteger ())
		{
			if (pos_err < ignoreCorrection->getValueDouble ())
			{
				conn->sendCommandEnd (DEVDEM_E_IGNORE, "ignoring correction as it is too small");
				return DEVDEM_E_COMMAND;
			}

			wcorrRaDec->setValueRaDec (total_cor_ra, total_cor_dec);

			posErr->setValueDouble (pos_err);
			sendValueAll (posErr);

			wCorrImgId->setValueInteger (img_id);
			sendValueAll (wCorrImgId);

			if (pos_err < smallCorrection->getValueDouble ()) {

			  logStream (MESSAGE_ERROR) << "NOT correcting pos_err" << pos_err<< sendLog;
			  conn->sendCommandEnd (DEVDEM_E_IGNORE, "ignoring correction debugging phase");

			  return -1;
			  //	return startResyncMove (conn, 1);
			}
			sendValueAll (wcorrRaDec);
			// else set BOP_EXPOSURE, and wait for result of statusUpdate call
			maskState (BOP_EXPOSURE, BOP_EXPOSURE, "blocking exposure for correction");
			// correction was accepted, will be carried once it will be possible
			return 0;
		}
		std::ostringstream _os;
		_os << "ignoring correction - cor_mark " << cor_mark << " moveNum " << moveNum->getValueInteger () << " corr_img " << corr_img << " corrImgId " << corrImgId->getValueInteger () << " img_id " << img_id << " wCorrImgId " << wCorrImgId->getValueInteger ();
		conn->sendCommandEnd (DEVDEM_E_IGNORE, _os.str ().c_str ());
		return -1;
	}
	else if (conn->isCommand (COMMAND_TELD_MOVE_TLE))
	{
		char *l1;
		char *l2;
		if (conn->paramNextString (&l1) || conn->paramNextString (&l2) || ! conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;

		// we would like to always calculate next TLE position
		tle_freeze->setValueBool (false);

		mpec->setValueString ("");
		tle_l1->setValueString (l1);
		tle_l2->setValueString (l2);

		ret = parse_elements (tle_l1->getValueString ().c_str (), tle_l2->getValueString ().c_str (), &tle);
		if (ret != 0)
		{
			logStream (MESSAGE_ERROR) << "cannot target on TLEs" << sendLog;
			logStream (MESSAGE_ERROR) << "Line 1: " << tle_l1->getValueString () << sendLog;
			logStream (MESSAGE_ERROR) << "Line 2: " << tle_l2->getValueString () << sendLog;
			return DEVDEM_E_PARAMSVAL;
		}

		int ephem = 1;

		int is_deep = select_ephemeris (&tle);
		if (is_deep && (ephem == 1 || ephem == 2))
			ephem += 2;	/* switch to an SDx */
		if (!is_deep && (ephem == 3 || ephem == 4))
			ephem -= 2;	/* switch to an SGx */
		tle_ephem->setValueInteger (ephem);

		startTracking (true);

		return startResyncMove (conn, 0);
	}
	else if (conn->isCommand (COMMAND_TELD_PARK))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		modelOn ();
		return startPark (conn);
	}
	else if (conn->isCommand ("stop"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		maskState (TEL_MASK_TRACK, TEL_NOTRACK, "tracking stopped");
		return stopMove ();
	}
	else if (conn->isCommand ("change"))
	{
		if (conn->paramNextHMS (&obj_ra) || conn->paramNextDMS (&obj_dec)
			|| !conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		offsRaDec->incValueRaDec (obj_ra, obj_dec);
		woffsRaDec->incValueRaDec (obj_ra, obj_dec);
		return startResyncMove (conn, 1);
	}
	else if (conn->isCommand ("save_model"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		ret = saveModel ();
		if (ret)
		{
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot save model");
		}
		return ret;
	}
	else if (conn->isCommand ("load_model"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		ret = loadModel ();
		if (ret)
		{
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot load model");
		}
		return ret;
	}
	else if (conn->isCommand ("worm_stop"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		stopMove ();
		maskState (TEL_MASK_TRACK, TEL_NOTRACK, "tracking stopped by worm_stop command");
		return 0;
	}
	else if (conn->isCommand ("worm_start"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		if (blockMove->getValueBool () == true || (getState () & TEL_MASK_MOVING) == TEL_PARKING || (getState () & TEL_MASK_MOVING) == TEL_PARKED)
		{
			ret = -1;
		}
		else
		{
			if (tracking->getValueInteger () == 0)
				tracking->setValueInteger (1);
			ret = setTracking (tracking->getValueInteger (), true);
		}

		if (ret == -1)
		{
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot start worm");
		}
		return ret;
	}
	else if (conn->isCommand ("reset"))
	{
		if (!conn->paramEnd ())
			return DEVDEM_E_PARAMSNUM;
		ret = resetMount ();
		if (ret == DEVDEM_E_COMMAND)
		{
			conn->sendCommandEnd (DEVDEM_E_HW, "cannot reset mount");
		}
		return ret;
	}
	return rts2core::Device::commandAuthorized (conn);
}

void Telescope::setFullBopState (rts2_status_t new_state)
{
	rts2core::Device::setFullBopState (new_state);
	if ((woffsRaDec->wasChanged () || wcorrRaDec->wasChanged ()) && !(new_state & BOP_TEL_MOVE))
		startResyncMove (NULL, (woffsRaDec->wasChanged () ? 1 : 0) | (wcorrRaDec->wasChanged () ? 2 : 0));
}

void Telescope::resetMpecTLE ()
{
	mpec->setValueString ("");
	tle_l1->setValueString ("");
	tle_l2->setValueString ("");
}
