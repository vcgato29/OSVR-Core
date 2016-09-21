/** @file
    @brief Implementation

    @date 2016

    @author
    Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2016 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Internal Includes
#include "RoomCalibration.h"
#include "Assumptions.h"
#include "ForEachTracked.h"
#include "TrackedBodyIMU.h"

// Library/third-party includes
#include <boost/assert.hpp>
#include <osvr/Util/EigenExtras.h>
#include <osvr/Util/ExtractYaw.h>

// Standard includes
#include <iostream>
#include <stdexcept>

namespace osvr {
namespace vbtracker {
    namespace filters = util::filters;

    using osvr::util::time::duration;

    /// These are the levels that the velocity must remain below for a given
    /// number of consecutive frames before we accept a correlation between the
    /// IMU and video as truth.
    static const auto LINEAR_VELOCITY_CUTOFF = 0.2;
    static const auto ANGULAR_VELOCITY_CUTOFF = 1.e-4;

    /// The number of low-velocity frames required
    static const std::size_t REQUIRED_SAMPLES = 10;

    /// The distance from the camera that we want to encourage users to move
    /// within for best initial startup. Provides the best view of beacons for
    /// initial start of autocalibration.
    static const auto NEAR_MESSAGE_CUTOFF = 0.4;

    RoomCalibration::RoomCalibration(Eigen::Vector3d const &camPosition,
                                     bool cameraIsForward)
        : m_lastVideoData(util::time::getNow()),
          m_suppliedCamPosition(camPosition),
          m_cameraIsForward(cameraIsForward) {}

    bool RoomCalibration::wantVideoData(TrackingSystem const &sys,
                                        BodyTargetId const &target) const {
        // This was once all in one conditional expression but it was almost
        // impossible for humans to parse, thus I leave it to computers to parse
        // this and optimize it.

        /// we only want video data once we have IMU
        if (!haveIMUData()) {
            return false;
        }
        if (!haveVideoData()) {
            /// If it's our first video data, the body ID should match with the
            /// IMU.
            return target.first == m_imuBody;
        }

        /// If it's not our first video data, it should be from the same target
        /// as our first video data.
        return m_videoTarget == target;
    }

    void RoomCalibration::processVideoData(
        TrackingSystem const &sys, BodyTargetId const &target,
        util::time::TimeValue const &timestamp, Eigen::Vector3d const &xlate,
        Eigen::Quaterniond const &quat) {
        if (!wantVideoData(sys, target)) {
            // early out if this is the wrong target, or we don't have IMU data
            // yet.
            return;
        }
        if (!xlate.array().allFinite() || !quat.coeffs().array().allFinite()) {
            // non-finite camera pose
            return;
        }
        if (!haveVideoData()) {
            msg() << "Got first video report from target " << target
                  << std::endl;
        }
        bool firstData = !haveVideoData();
        m_videoTarget = target;
        auto dt = duration(timestamp, m_lastVideoData);
        m_lastVideoData = timestamp;
        if (dt <= 0) {
            dt = 1; // in case of weirdness, avoid divide by zero.
        }

        // Pre-filter the camera data in case it's noisy (quite possible since
        // it's RANSAC)
        m_poseFilter.filter(dt, xlate,
#ifdef OSVR_FLIP_QUATS
            util::flipQuatSignToMatch(m_poseFilter.getOrientation(), quat)
#else
                            quat
#endif
                            );

        // Pose of tracked device (in camera space) is cTd
        // orientation is rTd or iTd: tracked device in IMU space (aka room
        // space, modulo yaw)
        // rTc is camera in room space (what we want to find), so we can take
        // camera-reported cTd, perform rTc * cTd, and end up with rTd with
        // position...
        // Eigen::Isometry3d rTc = m_imuOrientation * targetPose.inverse();
        Eigen::Isometry3d rTc =
            m_imuOrientation * m_poseFilter.getIsometry().inverse();

        // Feed this into the filter...
        m_cameraFilter.filter(dt, rTc.translation(),
                              Eigen::Quaterniond(rTc.rotation()));

        // Look at the velocity to see if the user was holding still enough.
        auto linearVel = m_cameraFilter.getLinearVelocityMagnitude();
        auto angVel = m_cameraFilter.getAngularVelocityMagnitude();

        // std::cout << "linear " << linearVel << " ang " << angVel << "\n";
        if (linearVel < LINEAR_VELOCITY_CUTOFF &&
            angVel < ANGULAR_VELOCITY_CUTOFF) {
            // OK, velocity within bounds
            if (m_steadyVideoReports == 0) {
                msg() << "Hold still, performing room calibration";
            }
            msgStream() << "." << std::flush;
            ++m_steadyVideoReports;
            if (finished()) {
                /// put an end to the dots
                msgStream() << "\n" << std::endl;
            }
        } else {
            handleExcessVelocity(xlate.z());
        }
    }
    void RoomCalibration::handleExcessVelocity(double zTranslation) {
        // reset the count if movement too fast.
        if (m_steadyVideoReports > 0) {
            /// put an end to the dots
            msgStream() << std::endl;

            // Look at the velocity to see if the user was holding still enough.
            auto linearVel = m_cameraFilter.getLinearVelocityMagnitude();
            msg() << "Restarting ";
            if (linearVel >= LINEAR_VELOCITY_CUTOFF) {
                msgStream() << " - Linear velocity too high (" << linearVel
                            << ")";
            }
            auto angVel = m_cameraFilter.getAngularVelocityMagnitude();
            if (angVel >= ANGULAR_VELOCITY_CUTOFF) {
                msgStream() << " - Angular velocity too high (" << angVel
                            << ")";
            }
            msgStream() << "\n";
        }
        m_steadyVideoReports = 0;
        switch (m_instructionState) {
        case InstructionState::Uninstructed:
            if (zTranslation > NEAR_MESSAGE_CUTOFF) {
                instructions()
                    << "NOTE: For best results, during tracker/server "
                       "startup, hold your head/HMD still closer than "
                    << NEAR_MESSAGE_CUTOFF
                    << " meters from the tracking camera for a few "
                       "seconds, then rotate slowly in all directions.";
                endInstructions();
                m_instructionState = InstructionState::ToldToMoveCloser;
            }
            break;
        case InstructionState::ToldToMoveCloser:
            if (zTranslation < (0.9 * NEAR_MESSAGE_CUTOFF)) {
                instructions() << "That distance looks good, rotate the device "
                                  "gently until you get a 'Hold still' "
                                  "message.";
                endInstructions();
                m_instructionState = InstructionState::ToldDistanceIsGood;
            }
            break;
        case InstructionState::ToldDistanceIsGood:
            // nothing to do now!
            break;
        }
    }
    void RoomCalibration::processIMUData(TrackingSystem const &sys,
                                         BodyId const &body,
                                         util::time::TimeValue const &timestamp,
                                         Eigen::Quaterniond const &quat) {
        if (haveIMUData() && m_imuBody != body) {
// Already got data from a different IMU
#ifdef OSVR_UVBI_ASSUME_SINGLE_IMU
            throw std::logic_error(
                "RoomCalibration just received data from a second IMU, but the "
                "single IMU assumption define is still in "
                "place!");
#endif
            return;
        }

        if (!quat.coeffs().array().allFinite()) {
            // non-finite quat
            msg() << "Non-finite quat" << std::endl;
            return;
        }
        bool first = false;
        if (!haveIMUData()) {

            if (!sys.isValidBodyId(body)) {
                msg() << "Invalid body ID" << std::endl;
                return;
            }
            auto &trackedBody = sys.getBody(body);
            if (trackedBody.getNumTargets() == 0) {
                // Can't use an IMU on a body with no video-based targets for
                // calibration.
                return;
            }
            // OK, so this is the first IMU report, fine with me.
            msg() << "Got first IMU report from body " << body.value()
                  << std::endl;
            m_imuBody = body;
            first = true;
        }
        BOOST_ASSERT_MSG(m_imuBody == body, "BodyID for incoming data and "
                                            "known IMU must be identical at "
                                            "this point");

        /// @todo something more elegant than just copying a quat?
#ifdef OSVR_FLIP_QUATS
        if (first) {
            // for setup purposes, we'll constrain w to be positive.
            m_imuOrientation =
                quat.w() >= 0. ? quat : Eigen::Quaterniond(-quat.coeffs());
        } else {
            m_imuOrientation =
                util::flipQuatSignToMatch(m_imuOrientation, quat);
        }
#else
        m_imuOrientation = quat;
#endif
    }

    bool RoomCalibration::postCalibrationUpdate(TrackingSystem &sys) {
        if (!finished()) {
            return false;
        }
        msg() << "Room calibration process complete." << std::endl;

        /// Coordinate systems involved here:
        /// i: IMU
        /// c: Camera
        /// r: Room
        /// Keep in mind the right to left convention: iTc is a transform that
        /// takes points from camera space to IMU space. Matching coordinate
        /// systems cancel: rTi * iTc = rTc

        /// We'll start by saying that the camera space orientation is unknown
        /// and that the IMU space orientation is aligned with the room, so we
        /// want that transformation. (That's what we've kept track of in a
        /// running fashion using the filter all along: iTc)
        ///
        /// We discard the positional component of iTc because that varies
        /// depending on where the device is held during calibration.
        ///
        /// If the user has set "camera is forward", we'll then extract the yaw
        /// component and create another transform so that the camera is always
        /// looking down the z axis of our room space: this will provide a
        /// rotation component of rTi.
        ///
        /// Finally, we will set the position of the camera based on the
        /// configuration.

        Eigen::Isometry3d iTc = getCameraToIMUCalibrationPoint();
        msg() << "Camera to calibration point: "
              << iTc.translation().transpose() << " rotation: "
              << Eigen::Quaterniond(iTc.rotation()).coeffs().transpose()
              << std::endl;
        Eigen::Quaterniond iRc = Eigen::Quaterniond(iTc.rotation());

        if (m_cameraIsForward) {
            auto yaw = util::extractYaw(iRc);
            m_imuYaw = -yaw * util::radians;
            iRc = Eigen::AngleAxisd(-yaw, Eigen::Vector3d::UnitY()) * iRc;
        } else {
            m_imuYaw = 0;
        }
#if 0
        m_imuYaw = 0 * util::radians;
        m_rTi = Eigen::Isometry3d::Identity();
#endif
#if 0
        iTc.translation() = Eigen::Vector3d::Zero();
        // Eigen::Isometry3d temp_rTc = iTc;

        Eigen::AngleAxisd rTi_rotation(0, Eigen::Vector3d::UnitY());
        if (m_cameraIsForward) {
            // "Reset Yaw" - the camera looks along the YZ plane.
            auto yaw = util::extractYaw(Eigen::Quaterniond(iTc.rotation()));
            m_imuYaw = -yaw * util::radians;
            rTi_rotation = Eigen::AngleAxisd(-yaw, Eigen::Vector3d::UnitY());
            // temp_rTc = rTi_rotation * iTc;
        }

        msg() << "rTi rotation: "
              << Eigen::Quaterniond(rTi_rotation).coeffs().transpose()
              << std::endl;
        // Account for the supplied camera position: m_suppliedCamPosition is
        // rTi translation.
        // Eigen::Vector3d cameraOffset =
        //    m_suppliedCamPosition - temp_rTc.translation();
        m_rTi = util::makeIsometry(m_suppliedCamPosition, rTi_rotation);

        msg() << "rTi: translation: " << m_rTi.translation().transpose()
              << "rotation: "
              << Eigen::Quaterniond(m_rTi.rotation()).coeffs().transpose()
              << std::endl;

        // cameraPose is rTc
        m_cameraPose = m_rTi * iTc;
#endif
        m_cameraPose = util::makeIsometry(m_suppliedCamPosition, iRc);
        msg() << "camera pose AKA rTc: translation: "
              << m_cameraPose.translation().transpose() << " rotation: ";
        Eigen::AngleAxisd rot(m_cameraPose.rotation());
        msgStream() << rot.angle() << " radians about "
                    << rot.axis().transpose() << std::endl;

        m_calibComplete = true;

        // Now go through the tracking system and pass along the information.
        sys.setCameraPose(getCameraPose());
        forEachIMU(sys, [&](TrackedBodyIMU &imu) {
            auto yaw = getCalibrationYaw(imu.getBody().getId());
            if (yaw) {
                imu.setCalibrationYaw(*yaw);
            }
        });
        return true;
    }

    boost::optional<util::Angle>
    RoomCalibration::getCalibrationYaw(BodyId const &body) const {
        BOOST_ASSERT_MSG(calibrationComplete(), "Not valid to call "
                                                "getCalibrationYaw() unless "
                                                "calibration is complete!");
        if (m_imuBody == body) {
            return m_imuYaw;
        }
        return boost::none;
    }

    Eigen::Isometry3d RoomCalibration::getCameraPose() const {
        BOOST_ASSERT_MSG(calibrationComplete(), "Not valid to call "
                                                "getCameraPose() unless "
                                                "calibration is complete!");
        return m_cameraPose;
    }
    Eigen::Isometry3d RoomCalibration::getIMUToRoom() const {
        BOOST_ASSERT_MSG(calibrationComplete(), "Not valid to call "
                                                "getIMUToRoom() unless "
                                                "calibration is complete!");
        return m_rTi;
    }

    bool RoomCalibration::finished() const {
        return m_steadyVideoReports >= REQUIRED_SAMPLES;
    }

    Eigen::Isometry3d RoomCalibration::getCameraToIMUCalibrationPoint() const {
        return m_cameraFilter.getIsometry();
    }

    std::ostream &RoomCalibration::msgStream() const { return std::cout; }
    std::ostream &RoomCalibration::msg() const {
        return msgStream() << "[Unified Tracker: Room Calibration] ";
    }

    std::ostream &RoomCalibration::instructions() const {
        msgStream() << "\n\n";
        return msg();
    }
    void RoomCalibration::endInstructions() const {
        msgStream() << "\n\n" << std::endl;
    }
    bool isRoomCalibrationComplete(TrackingSystem const &sys) {
        // Check for camera pose
        if (!sys.haveCameraPose()) {
            return false;
        }

        // Check all IMUs.
        auto numIMUs = std::size_t{0};
        bool complete = true;
        forEachIMU(sys, [&](TrackedBodyIMU const &imu) {
            complete = complete && imu.calibrationYawKnown();
            ++numIMUs;
        });

#ifdef OSVR_UVBI_ASSUME_SINGLE_IMU
        if (numIMUs > 1) {
            throw std::logic_error("More than one IMU system wide, but the "
                                   "single IMU assumption define is still in "
                                   "place!");
        }
#endif
        return complete;
    }
} // namespace vbtracker
} // namespace osvr
