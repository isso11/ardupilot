#pragma once

#include <AP_Common/AP_Common.h>
#include <AP_Param/AP_Param.h>
#include <AP_Math/AP_Math.h>
#include <AC_AttitudeControl/AC_PosControl.h>      // Position control library

// loiter maximum velocities and accelerations
#define AC_CIRCLE_RADIUS_DEFAULT    1000.0f     // radius of the circle in cm that the vehicle will fly
#define AC_CIRCLE_RATE_DEFAULT      20.0f       // turn rate in deg/sec.  Positive to turn clockwise, negative for counter clockwise
#define AC_CIRCLE_ANGULAR_ACCEL_MIN 2.0f        // angular acceleration should never be less than 2deg/sec
#define AC_CIRCLE_RADIUS_MAX        200000.0f   // maximum allowed circle radius of 2km

class AC_Circle
{
public:

    /// Constructor
    AC_Circle(const AP_AHRS_View& ahrs, AC_PosControl& pos_control);

    /// init - initialise circle controller setting center specifically
    ///     set is_terrain_alt to true if center_neu_cm.z should be interpreted as an alt-above-terrain. Rate should be +ve in deg/sec for cw turn
    ///     caller should set the position controller's x,y and z speeds and accelerations before calling this
    void init_NEU_cm(const Vector3p& center_neu_cm, bool is_terrain_alt, float rate_degs);

    /// init - initialise circle controller setting center using stopping point and projecting out based on the copter's heading
    ///     caller should set the position controller's x,y and z speeds and accelerations before calling this
    void init();

    /// set circle center to a Location
    void set_center(const Location& center);

    /// set_circle_center as a vector from ekf origin
    ///     is_terrain_alt should be true if center.z is alt is above terrain
    void set_center_NEU_cm(const Vector3f& center_neu_cm, bool is_terrain_alt) { _center_neu_cm = center_neu_cm.topostype(); _is_terrain_alt = is_terrain_alt; }

    /// get_circle_center in cm from home
    const Vector3p& get_center_NEU_cm() const { return _center_neu_cm; }

    /// returns true if using terrain altitudes
    bool center_is_terrain_alt() const { return _is_terrain_alt; }

    /// get_radius - returns radius of circle in cm
    float get_radius_cm() const { return is_positive(_radius_cm)?_radius_cm:_radius_parm_cm; }

    /// set_radius_cm - sets circle radius in cm
    void set_radius_cm(float radius_cm);

    /// get_rate_degs - returns target rate in deg/sec held in RATE parameter
    float get_rate_degs() const { return _rate_degs; }

    /// get_rate_current - returns actual calculated rate target in deg/sec, which may be less than _rate_degs
    float get_rate_current() const { return degrees(_angular_vel_rads); }

    /// set_rate - set circle rate in degrees per second
    void set_rate_degs(float rate_degs);

    /// get_angle_total_rad - return total angle in radians that vehicle has circled
    float get_angle_total_rad() const { return _angle_total_rad; }

    /// update - update circle controller
    ///     returns false on failure which indicates a terrain failsafe
    bool update_cms(float climb_rate_cms = 0.0f) WARN_IF_UNUSED;

    /// get desired roll, pitch which should be fed into stabilize controllers
    float get_roll_cd() const { return _pos_control.get_roll_cd(); }
    float get_pitch_cd() const { return _pos_control.get_pitch_cd(); }
    Vector3f get_thrust_vector() const { return _pos_control.get_thrust_vector(); }
    float get_yaw_cd() const { return _yaw_cd; }
    float get_yaw_rad() const { return cd_to_rad(_yaw_cd); }

    /// returns true if update has been run recently
    /// used by vehicle code to determine if get_yaw() is valid
    bool is_active() const;

    // get_closest_point_on_circle_NEU_cm - returns closest point on the circle
    //      circle's center should already have been set
    //      closest point on the circle will be placed in result, dist_cm will be updated with the distance to the center
    //      result's altitude (i.e. z) will be set to the circle_center's altitude
    //      if vehicle is at the center of the circle, the edge directly behind vehicle will be returned
    void get_closest_point_on_circle_NEU_cm(Vector3f& result_NEU_cm, float& dist_cm) const;

    /// get horizontal distance to loiter target in cm
    float get_distance_to_target_cm() const { return _pos_control.get_pos_error_NE_cm(); }

    /// get bearing to target in degrees
    float get_bearing_to_target_rad() const { return _pos_control.get_bearing_to_target_rad(); }

    /// true if pilot control of radius and turn rate is enabled
    bool pilot_control_enabled() const { return (_options.get() & CircleOptions::MANUAL_CONTROL) != 0; }

    /// true if mount roi is at circle center
    bool roi_at_center() const { return (_options.get() & CircleOptions::ROI_AT_CENTER) != 0; }

    /// provide rangefinder based terrain offset
    /// terrain offset is the terrain's height above the EKF origin
    void set_rangefinder_terrain_offset_cm(bool use, bool healthy, float terrain_offset_cm) { _rangefinder_available = use; _rangefinder_healthy = healthy; _rangefinder_terrain_offset_cm = terrain_offset_cm;}

    /// check for a change in the radius params
    void check_param_change();

    static const struct AP_Param::GroupInfo var_info[];

private:

    // calc_velocities - calculate angular velocity max and acceleration based on radius and rate
    //      this should be called whenever the radius or rate are changed
    //      initialises the yaw and current position around the circle
    //      init_velocity should be set true if vehicle is just starting circle
    void calc_velocities(bool init_velocity);

    // init_start_angle - sets the starting angle around the circle and initialises the angle_total
    //      if use_heading is true the vehicle's heading will be used to init the angle causing minimum yaw movement
    //      if use_heading is false the vehicle's position from the center will be used to initialise the angle
    void init_start_angle(bool use_heading);

    // get expected source of terrain data
    enum class TerrainSource {
        TERRAIN_UNAVAILABLE,
        TERRAIN_FROM_RANGEFINDER,
        TERRAIN_FROM_TERRAINDATABASE,
    };
    AC_Circle::TerrainSource get_terrain_source() const;

    // get terrain's altitude (in cm above the ekf origin) at the current position (+ve means terrain below vehicle is above ekf origin's altitude)
    bool get_terrain_offset_cm(float& offset_cm);

    // flags structure
    struct circle_flags {
        uint8_t panorama    : 1;    // true if we are doing a panorama
    } _flags;

    // references to inertial nav and ahrs libraries
    const AP_AHRS_View&         _ahrs;
    AC_PosControl&              _pos_control;

    enum CircleOptions {
        MANUAL_CONTROL           = 1U << 0,
        FACE_DIRECTION_OF_TRAVEL = 1U << 1,
        INIT_AT_CENTER           = 1U << 2, // true then the circle center will be the current location, false and the center will be 1 radius ahead
        ROI_AT_CENTER            = 1U << 3, // true when the mount roi is at circle center
    };

    // parameters
    AP_Float    _radius_parm_cm;    // radius of circle in cm loaded from params
    AP_Float    _rate_parm_degs;    // rotation speed in deg/sec
    AP_Int16    _options;           // stick control enable/disable

    // internal variables
    Vector3p    _center_neu_cm;         // center of circle in cm from home
    float       _radius_cm;             // radius of circle in cm
    float       _rate_degs;             // rotation speed of circle in deg/sec. +ve for cw turn
    float       _yaw_cd;                // yaw heading (normally towards circle center)
    float       _angle_rad;             // current angular position around circle in radians (0=directly north of the center of the circle)
    float       _angle_total_rad;       // total angle traveled in radians
    float       _angular_vel_rads;      // angular velocity in radians/sec
    float       _angular_vel_max_rads;  // maximum velocity in radians/sec
    float       _angular_accel_radss;   // angular acceleration in radians/sec/sec
    uint32_t    _last_update_ms;        // system time of last update
    float       _last_radius_param;     // last value of radius param, used to update radius on param change

    // terrain following variables
    bool        _is_terrain_alt;                // true if _center_neu_cm.z is alt-above-terrain, false if alt-above-ekf-origin
    bool        _rangefinder_available;         // true if range finder could be used
    bool        _rangefinder_healthy;           // true if range finder is healthy
    float       _rangefinder_terrain_offset_cm; // latest rangefinder based terrain offset (e.g. terrain's height above EKF origin)
};
