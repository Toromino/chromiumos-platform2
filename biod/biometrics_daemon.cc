// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biometrics_daemon.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <brillo/dbus/async_event_sequencer.h>

#include "biod/fake_biometric.h"
#include "biod/fpc_biometric.h"

namespace biod {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusInterface;
using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::ExportedObjectManager;
using brillo::dbus_utils::ExportedProperty;
using brillo::dbus_utils::ExportedPropertySet;
using dbus::ObjectPath;

namespace dbus_constants {
const char kBusServiceName[] = "org.freedesktop.DBus";
const char kBusServicePath[] = "/org/freedesktop/DBus";
const char kBusInterface[] = "org.freedesktop.DBus";
const char kServiceName[] = "org.chromium.BiometricsDaemon";
const char kServicePath[] = "/org/chromium/BiometricsDaemon";
const char kBiometricInterface[] = "org.chromium.BiometricsDaemon.Biometric";
const char kAuthenticationInterface[] =
    "org.chromium.BiometricsDaemon.Authentication";
const char kEnrollInterface[] = "org.chromium.BiometricsDaemon.Enroll";
const char kEnrollmentInterface[] = "org.chromium.BiometricsDaemon.Enrollment";
}

namespace errors {
const char kDomain[] = "biod";
const char kInternalError[] = "internal_error";
const char kInvalidArguments[] = "invalid_arguments";
}

void LogOnSignalConnected(const std::string& interface_name,
                          const std::string& signal_name,
                          bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
}

BiometricWrapper::BiometricWrapper(
    std::unique_ptr<Biometric> biometric,
    ExportedObjectManager* object_manager,
    ObjectPath object_path,
    const AsyncEventSequencer::CompletionAction& completion_callback)
    : biometric_(std::move(biometric)),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(std::move(object_path)),
      enroll_object_path_(object_path_.value() + "/Enroll"),
      authentication_object_path_(object_path_.value() + "/Authentication") {
  CHECK(biometric_);

  biometric_->SetScannedHandler(
      base::Bind(&BiometricWrapper::OnScanned, base::Unretained(this)));
  biometric_->SetAttemptHandler(
      base::Bind(&BiometricWrapper::OnAttempt, base::Unretained(this)));
  biometric_->SetFailureHandler(
      base::Bind(&BiometricWrapper::OnFailure, base::Unretained(this)));

  dbus::ObjectProxy* bus_proxy = object_manager->GetBus()->GetObjectProxy(
      dbus_constants::kBusServiceName,
      dbus::ObjectPath(dbus_constants::kBusServicePath));
  bus_proxy->ConnectToSignal(
      dbus_constants::kBusInterface,
      "NameOwnerChanged",
      base::Bind(&BiometricWrapper::OnNameOwnerChanged, base::Unretained(this)),
      base::Bind(&LogOnSignalConnected));

  DBusInterface* bio_interface =
      dbus_object_.AddOrGetInterface(dbus_constants::kBiometricInterface);
  property_type_.SetValue(static_cast<uint32_t>(biometric_->GetType()));
  bio_interface->AddProperty("Type", &property_type_);
  bio_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      "StartEnroll",
      base::Bind(&BiometricWrapper::StartEnroll, base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithError(
      "GetEnrollments",
      base::Bind(&BiometricWrapper::GetEnrollments, base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithError(
      "DestroyAllEnrollments",
      base::Bind(&BiometricWrapper::DestroyAllEnrollments,
                 base::Unretained(this)));
  bio_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      "StartAuthentication",
      base::Bind(&BiometricWrapper::StartAuthentication,
                 base::Unretained(this)));
  dbus_object_.RegisterAsync(completion_callback);

  RefreshEnrollmentObjects();
}

BiometricWrapper::EnrollmentWrapper::EnrollmentWrapper(
    BiometricWrapper* biometric,
    std::unique_ptr<Biometric::Enrollment> enrollment,
    ExportedObjectManager* object_manager,
    const ObjectPath& object_path)
    : biometric_(biometric),
      enrollment_(std::move(enrollment)),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(object_path) {
  DBusInterface* enrollment_interface =
      dbus_object_.AddOrGetInterface(dbus_constants::kEnrollmentInterface);
  property_label_.SetValue(enrollment_->GetLabel());
  enrollment_interface->AddProperty("Label", &property_label_);
  enrollment_interface->AddSimpleMethodHandlerWithError(
      "SetLabel",
      base::Bind(&EnrollmentWrapper::SetLabel, base::Unretained(this)));
  enrollment_interface->AddSimpleMethodHandlerWithError(
      "Remove", base::Bind(&EnrollmentWrapper::Remove, base::Unretained(this)));
  dbus_object_.RegisterAndBlock();
}

BiometricWrapper::EnrollmentWrapper::~EnrollmentWrapper() {
  dbus_object_.UnregisterAsync();
}

bool BiometricWrapper::EnrollmentWrapper::SetLabel(
    brillo::ErrorPtr* error, const std::string& new_label) {
  if (!enrollment_->SetLabel(new_label)) {
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to set label");
    return false;
  }
  property_label_.SetValue(new_label);
  return true;
}

bool BiometricWrapper::EnrollmentWrapper::Remove(brillo::ErrorPtr* error) {
  if (!enrollment_->Remove()) {
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to remove enrollment");
    return false;
  }
  biometric_->RefreshEnrollmentObjects();
  return true;
}

void BiometricWrapper::FinalizeEnrollObject() {
  enroll_owner_.clear();
  enroll_dbus_object_->UnregisterAsync();
  enroll_dbus_object_.reset();
}

void BiometricWrapper::FinalizeAuthenticationObject() {
  authentication_owner_.clear();
  authentication_dbus_object_->UnregisterAsync();
  authentication_dbus_object_.reset();
}

void BiometricWrapper::OnNameOwnerChanged(dbus::Signal* sig) {
  dbus::MessageReader reader(sig);
  std::string name, old_owner, new_owner;
  if (!reader.PopString(&name) || !reader.PopString(&old_owner) ||
      !reader.PopString(&new_owner)) {
    LOG(ERROR) << "Received invalid NameOwnerChanged signal";
    return;
  }

  // We are only interested in cases where a name gets dropped from D-Bus.
  if (name.empty() || !new_owner.empty())
    return;

  // If one of the session was owned by the dropped name, the session should
  // also be dropped, as there is nobody left to end it explicitly.

  if (name == enroll_owner_) {
    LOG(INFO) << "Enroll object owner " << enroll_owner_
              << " has died. Enrollment is canceled automatically.";
    if (enroll_)
      enroll_.End();

    if (enroll_dbus_object_)
      FinalizeEnrollObject();
  }

  if (name == authentication_owner_) {
    LOG(INFO) << "Authentication object owner " << authentication_owner_
              << " has died. Authentication is ended automatically.";
    if (authentication_)
      authentication_.End();

    if (authentication_dbus_object_)
      FinalizeAuthenticationObject();
  }
}

void BiometricWrapper::OnScanned(Biometric::ScanResult scan_result, bool done) {
  if (enroll_dbus_object_) {
    dbus::Signal scanned_signal(dbus_constants::kBiometricInterface, "Scanned");
    dbus::MessageWriter writer(&scanned_signal);
    writer.AppendUint32(static_cast<uint32_t>(scan_result));
    writer.AppendBool(done);
    dbus_object_.SendSignal(&scanned_signal);
    if (done) {
      FinalizeEnrollObject();
      RefreshEnrollmentObjects();
    }
  }
}

void BiometricWrapper::OnAttempt(Biometric::ScanResult scan_result,
                                 std::vector<std::string> recognized_user_ids) {
  if (authentication_dbus_object_) {
    dbus::Signal attempt_signal(dbus_constants::kBiometricInterface, "Attempt");
    dbus::MessageWriter writer(&attempt_signal);
    writer.AppendUint32(static_cast<uint32_t>(scan_result));
    writer.AppendArrayOfStrings(recognized_user_ids);
    dbus_object_.SendSignal(&attempt_signal);
  }
}

void BiometricWrapper::OnFailure() {
  const char kFailureSignal[] = "Failure";

  if (enroll_dbus_object_) {
    dbus::Signal failure_signal(dbus_constants::kBiometricInterface,
                                kFailureSignal);
    dbus_object_.SendSignal(&failure_signal);
    FinalizeEnrollObject();
  }
  if (authentication_dbus_object_) {
    dbus::Signal failure_signal(dbus_constants::kBiometricInterface,
                                kFailureSignal);
    dbus_object_.SendSignal(&failure_signal);
    FinalizeAuthenticationObject();
  }
}

bool BiometricWrapper::StartEnroll(brillo::ErrorPtr* error,
                                   dbus::Message* message,
                                   const std::string& user_id,
                                   const std::string& label,
                                   ObjectPath* enroll_path) {
  Biometric::EnrollSession enroll = biometric_->StartEnroll(user_id, label);
  if (!enroll) {
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start enroll");
    return false;
  }
  enroll_ = std::move(enroll);

  enroll_dbus_object_.reset(
      new DBusObject(NULL, dbus_object_.GetBus(), enroll_object_path_));
  DBusInterface* enroll_interface =
      enroll_dbus_object_->AddOrGetInterface(dbus_constants::kEnrollInterface);
  enroll_interface->AddSimpleMethodHandlerWithError(
      "Cancel",
      base::Bind(&BiometricWrapper::EnrollCancel, base::Unretained(this)));
  enroll_dbus_object_->RegisterAndBlock();
  *enroll_path = enroll_object_path_;
  enroll_owner_ = message->GetSender();

  return true;
}

bool BiometricWrapper::GetEnrollments(brillo::ErrorPtr* error,
                                      std::vector<ObjectPath>* out) {
  std::vector<std::unique_ptr<Biometric::Enrollment>> enrollments =
      biometric_->GetEnrollments();
  out->resize(enrollments.size());
  std::transform(enrollments_.begin(),
                 enrollments_.end(),
                 out->begin(),
                 [this](std::unique_ptr<EnrollmentWrapper>& enrollment) {
                   return enrollment->path();
                 });
  return true;
}

bool BiometricWrapper::DestroyAllEnrollments(brillo::ErrorPtr* error) {
  if (!biometric_->DestroyAllEnrollments()) {
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to destroy all enrollments");
    return false;
  }
  RefreshEnrollmentObjects();
  return true;
}

bool BiometricWrapper::StartAuthentication(brillo::ErrorPtr* error,
                                           dbus::Message* message,
                                           ObjectPath* authentication_path) {
  Biometric::AuthenticationSession authentication =
      biometric_->StartAuthentication();
  if (!authentication) {
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start authentication");
    return false;
  }
  authentication_ = std::move(authentication);

  authentication_dbus_object_.reset(
      new DBusObject(NULL, dbus_object_.GetBus(), authentication_object_path_));
  DBusInterface* authentication_interface =
      authentication_dbus_object_->AddOrGetInterface(
          dbus_constants::kAuthenticationInterface);
  authentication_interface->AddSimpleMethodHandlerWithError(
      "End",
      base::Bind(&BiometricWrapper::AuthenticationEnd, base::Unretained(this)));
  authentication_dbus_object_->RegisterAndBlock();
  *authentication_path = authentication_object_path_;
  authentication_owner_ = message->GetSender();

  return true;
}

bool BiometricWrapper::EnrollCancel(brillo::ErrorPtr* error) {
  if (!enroll_) {
    LOG(WARNING) << "DBus client attempted to cancel null enrollment";
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInvalidArguments,
                                   "Enroll object was null");
    return false;
  }
  enroll_.End();
  if (enroll_dbus_object_) {
    FinalizeEnrollObject();
  }
  return true;
}

bool BiometricWrapper::AuthenticationEnd(brillo::ErrorPtr* error) {
  if (!authentication_) {
    LOG(WARNING) << "DBus client attempted to cancel null authentication";
    *error = brillo::Error::Create(FROM_HERE,
                                   errors::kDomain,
                                   errors::kInvalidArguments,
                                   "Authentication object was null");
    return false;
  }
  authentication_.End();
  if (authentication_dbus_object_) {
    FinalizeAuthenticationObject();
  }
  return true;
}

void BiometricWrapper::RefreshEnrollmentObjects() {
  enrollments_.clear();
  std::vector<std::unique_ptr<Biometric::Enrollment>> enrollments =
      biometric_->GetEnrollments();

  ExportedObjectManager* object_manager = dbus_object_.GetObjectManager().get();
  std::string enrollments_root_path =
      object_path_.value() + std::string("/Enrollment");

  for (std::unique_ptr<Biometric::Enrollment>& enrollment : enrollments) {
    ObjectPath enrollment_path(enrollments_root_path +
                               std::to_string(enrollment->GetId()));
    enrollments_.emplace_back(new EnrollmentWrapper(
        this, std::move(enrollment), object_manager, enrollment_path));
  }
}

BiometricsDaemon::BiometricsDaemon() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";

  object_manager_.reset(new ExportedObjectManager(
      bus_, ObjectPath(dbus_constants::kServicePath)));

  scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());
  object_manager_->RegisterAsync(
      sequencer->GetHandler("Manager.RegisterAsync() failed.", true));

  ObjectPath fake_bio_path =
      ObjectPath(dbus_constants::kServicePath + std::string("/FakeBiometric"));
  biometrics_.emplace_back(new BiometricWrapper(
      std::unique_ptr<Biometric>(new FakeBiometric),
      object_manager_.get(),
      fake_bio_path,
      sequencer->GetHandler("Failed to register biometric object", true)));

  ObjectPath fpc_bio_path =
      ObjectPath(dbus_constants::kServicePath + std::string("/FpcBiometric"));
  std::unique_ptr<Biometric> fpc_bio = FpcBiometric::Create();
  CHECK(fpc_bio);
  biometrics_.emplace_back(new BiometricWrapper(
      std::move(fpc_bio),
      object_manager_.get(),
      fpc_bio_path,
      sequencer->GetHandler("Failed to register biometric object", true)));

  CHECK(bus_->RequestOwnershipAndBlock(dbus_constants::kServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
}

}  // namespace biod
