// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gobi_gsm_modem.h"

#include "gobi_modem_handler.h"
#include <base/scoped_ptr.h>
#include <cromo/carrier.h>
#include <cromo/sms_message.h>
#include <mm/mm-modem.h>

#include <sstream>

//======================================================================
// Construct and destruct
GobiGsmModem::~GobiGsmModem() {
}

//======================================================================
// Callbacks and callback utilities

void GobiGsmModem::SignalStrengthHandler(INT8 signal_strength,
                                         ULONG radio_interface) {
  unsigned long ss_percent = MapDbmToPercent(signal_strength);

  DLOG(INFO) << "SignalStrengthHandler " << static_cast<int>(signal_strength)
             << " dBm on radio interface " << radio_interface
             << " (" << ss_percent << "%)";

  // TODO(ers) make sure radio interface corresponds to the network
  // on which we're registered
  SignalQuality(ss_percent);  // NB:  org.freedesktop...Modem.Gsm.Network

  // See whether we're going from no signal to signal. If so, that's an
  // indication that we're now registered on a network, so get registration
  // info and send it out.
  if (!signal_available_) {
    signal_available_ = true;
    RegistrationStateHandler();
  }
}

void GobiGsmModem::RegistrationStateHandler() {
  LOG(INFO) << "RegistrationStateHandler";
  uint32_t registration_status;
  std::string operator_code;
  std::string operator_name;
  DBus::Error error;

  LOG(INFO) << "RegistrationStateHandler";
  GetGsmRegistrationInfo(&registration_status,
                         &operator_code, &operator_name, error);
  if (!error.is_set())
    RegistrationInfo(registration_status, operator_code, operator_name);
}

#define MASKVAL(cap) (1 << static_cast<int>(cap))
#define HASCAP(mask, cap) (mask & MASKVAL(cap))

static uint32_t DataCapabilitiesToMmAccessTechnology(BYTE num_data_caps,
                                                     ULONG* data_caps) {
  uint32_t capmask = 0;
  if (num_data_caps == 0)  // TODO(ers) indicates not registered?
    return MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
  // Put the values into a bit mask, where they'll be easier
  // to work with.
  for (int i = 0; i < num_data_caps; i++) {
    LOG(INFO) << "  Cap: " << static_cast<int>(data_caps[i]);
    capmask |= 1 << static_cast<int>(data_caps[i]);
  }
  // Of the data capabilities reported, select the one with the
  // highest theoretical bandwidth.
  uint32_t mm_access_tech;
  switch (capmask & (MASKVAL(gobi::kDataCapHsdpa) |
                     (MASKVAL(gobi::kDataCapHsupa)))) {
    case MASKVAL(gobi::kDataCapHsdpa) | MASKVAL(gobi::kDataCapHsupa):
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSPA;
      break;
    case MASKVAL(gobi::kDataCapHsupa):
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
      break;
    case MASKVAL(gobi::kDataCapHsdpa):
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
      break;
    default:
      if (HASCAP(capmask, gobi::kDataCapWcdma))
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_UMTS;
      else if (HASCAP(capmask, gobi::kDataCapEdge))
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_EDGE;
      else if (HASCAP(capmask, gobi::kDataCapGprs))
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_GPRS;
      else if (HASCAP(capmask, gobi::kDataCapGsm))
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_GSM;
      else
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
      break;
  }
  LOG(INFO) << "MM access tech: " << mm_access_tech;
  return mm_access_tech;
}

#undef MASKVAL
#undef HASCAP

void GobiGsmModem::DataCapabilitiesHandler(BYTE num_data_caps,
                                           ULONG* data_caps) {
  LOG(INFO) << "GsmDataCapabilitiesHandler";
  SendNetworkTechnologySignal(
      DataCapabilitiesToMmAccessTechnology(num_data_caps, data_caps));
}

void GobiGsmModem::DataBearerTechnologyHandler(ULONG technology) {
  uint32_t mm_access_tech;
  LOG(INFO) << "DataBearerTechnologyHandler: " << technology;
  switch (technology) {
    case gobi::kDataBearerGprs:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_GPRS;
      break;
    case gobi::kDataBearerWcdma:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_UMTS;
      break;
    case gobi::kDataBearerEdge:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_EDGE;
      break;
    case gobi::kDataBearerHsdpaDlWcdmaUl:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSDPA;
      break;
    case gobi::kDataBearerWcdmaDlUsupaUl:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSUPA;
      break;
    case gobi::kDataBearerHsdpaDlHsupaUl:
      mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_HSPA;
      break;
    default:
        mm_access_tech = MM_MODEM_GSM_ACCESS_TECH_UNKNOWN;
        break;
  }
  SendNetworkTechnologySignal(mm_access_tech);
}

void GobiGsmModem::SendNetworkTechnologySignal(uint32_t mm_access_tech) {
  if (mm_access_tech != MM_MODEM_GSM_ACCESS_TECH_UNKNOWN) {
    AccessTechnology = mm_access_tech;
    utilities::DBusPropertyMap props;
    props["AccessTechnology"].writer().append_uint32(mm_access_tech);
    MmPropertiesChanged(
        org::freedesktop::ModemManager::Modem::Gsm::Network_adaptor
        ::introspect()->name, props);
  }
}

gboolean GobiGsmModem::CheckDataCapabilities(gpointer data) {
  CallbackArgs* args = static_cast<CallbackArgs*>(data);
  GobiGsmModem* modem =
      static_cast<GobiGsmModem *>(handler_->LookupByPath(*args->path));
  delete args;
  if (modem != NULL)
    modem->SendNetworkTechnologySignal(modem->GetMmAccessTechnology());
  return FALSE;
}

gboolean GobiGsmModem::NewSmsCallback(gpointer data) {
  NewSmsArgs* args = static_cast<NewSmsArgs*>(data);
  LOG(INFO) << "New SMS Callback: type " << args->storage_type
            << " index " << args->message_index;
  GobiGsmModem* modem =
      static_cast<GobiGsmModem *>(handler_->LookupByPath(*args->path));
  if (modem == NULL)
    return FALSE;
  modem->SmsReceived(args->message_index, true);
  return FALSE;
}

void GobiGsmModem::RegisterCallbacks() {
  GobiModem::RegisterCallbacks();
  sdk_->SetNewSMSCallback(NewSmsCallbackTrampoline);
}

static std::string MakeOperatorCode(WORD mcc, WORD mnc) {
  std::ostringstream opercode;
  std::string result;
  if (mcc != 0xffff && mnc != 0xffff)
    opercode << mcc << mnc;
  return opercode.str();
}

// returns <registration status, operator code, operator name>
void GobiGsmModem::GetGsmRegistrationInfo(uint32_t* registration_state,
                                          std::string* operator_code,
                                          std::string* operator_name,
                                          DBus::Error& error) {
  ULONG reg_state, roaming_state;
  ULONG l1;
  WORD mcc, mnc;
  CHAR netname[32];
  BYTE radio_interfaces[10];
  BYTE num_radio_interfaces = sizeof(radio_interfaces)/sizeof(BYTE);

  ULONG rc = sdk_->GetServingNetwork(&reg_state, &l1, &num_radio_interfaces,
                                     radio_interfaces, &roaming_state,
                                     &mcc, &mnc, sizeof(netname), netname);
  ENSURE_SDK_SUCCESS(GetServingNetwork, rc, kSdkError);

  switch (reg_state) {
    case gobi::kUnregistered:
      *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_IDLE;
      break;
    case gobi::kRegistered:
      // TODO(ers) should RoamingPartner be reported as HOME?
      if (roaming_state == gobi::kHome)
        *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_HOME;
      else
        *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_ROAMING;
      break;
    case gobi::kSearching:
      *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_SEARCHING;
      break;
    case gobi::kRegistrationDenied:
      *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_DENIED;
      break;
    case gobi::kRegistrationStateUnknown:
      *registration_state = MM_MODEM_GSM_NETWORK_REG_STATUS_UNKNOWN;
      break;
  }
  *operator_code = MakeOperatorCode(mcc, mnc);
  // trim operator name
  *operator_name = netname;
  std::string::size_type pos1 = operator_name->find_first_not_of(' ');
  if (pos1 != std::string::npos) {
    operator_name->erase(0, pos1);
    std::string::size_type pos2 = operator_name->find_last_not_of(' ');
    if (pos2 != std::string::npos) {
      operator_name->erase(pos2+1);
    }
  } else {
    operator_name->erase(0);
  }
  LOG(INFO) << "GSM reg info: "
            << *registration_state << ", "
            << *operator_code << ", "
            << *operator_name;
}

// Determine the current network technology and map it to
// ModemManager's MM_MODEM_GSM_ACCESS_TECH enum
uint32_t GobiGsmModem::GetMmAccessTechnology() {
  BYTE data_caps[48];
  BYTE num_data_caps = 12;
  DBus::Error error;

  ULONG rc = sdk_->GetServingNetworkCapabilities(&num_data_caps, data_caps);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetServingNetworkCapabilities, rc, kSdkError,
                                 MM_MODEM_GSM_ACCESS_TECH_UNKNOWN);

  return DataCapabilitiesToMmAccessTechnology(
      num_data_caps,
      reinterpret_cast<ULONG*>(data_caps));
}

void GobiGsmModem::SetTechnologySpecificProperties() {
  AccessTechnology = GetMmAccessTechnology();
  // TODO(ers) also need to set AllowedModes property. For the Gsm.Card
  // interface, need to set SupportedBands and SupportedModes properties
}

void GobiGsmModem::GetTechnologySpecificStatus(
    utilities::DBusPropertyMap* properties) {
}

//======================================================================
// DBUS Methods: Modem.Gsm.Network

void GobiGsmModem::Register(const std::string& network_id,
                            DBus::Error& error) {
  // TODO(ers) For now, ignore network_id, and only do automatic registration
  // This is a blocking call, and may take a while (up to 30 seconds)
  ULONG rc = sdk_->InitiateNetworkRegistration(
      gobi::kRegistrationTypeAutomatic, 0, 0, 0);
  if (rc == gobi::kOperationHasNoEffect)
    return;  // already registered on requested network
  ENSURE_SDK_SUCCESS(InitiateNetworkRegistration, rc, kSdkError);
}

ScannedNetworkList GobiGsmModem::Scan(DBus::Error& error) {
  gobi::GsmNetworkInfoInstance networks[4];
  BYTE num_networks = sizeof(networks)/sizeof(networks[0]);
  ScannedNetworkList list;

  // This is a blocking call, and may take a while (i.e., a minute or more)
  ULONG rc = sdk_->PerformNetworkScan(&num_networks,
                                      static_cast<BYTE*>((void *)&networks[0]));
  ENSURE_SDK_SUCCESS_WITH_RESULT(PerformNetworkScan, rc, kSdkError, list);
  return list;
}

void GobiGsmModem::SetApn(const std::string& apn, DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SetApn not implemented";
}

uint32_t GobiGsmModem::GetSignalQuality(DBus::Error& error) {
  return GobiModem::CommonGetSignalQuality(error);
}

void GobiGsmModem::SetBand(const uint32_t& band, DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SetBand not implemented";
}

uint32_t GobiGsmModem::GetBand(DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::GetBand not implemented";
  return 0;
}

void GobiGsmModem::SetNetworkMode(const uint32_t& mode, DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SetNetworkMode not implemented";
}

uint32_t GobiGsmModem::GetNetworkMode(DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::GetNetworkMode not implemented";
  return 0;
}

// returns <registration status, operator code, operator name>
// reg status = idle, home, searching, denied, unknown, roaming
DBus::Struct<uint32_t,
    std::string,
    std::string> GobiGsmModem::GetRegistrationInfo(
    DBus::Error& error) {
  DBus::Struct<uint32_t, std::string, std::string> result;
  GetGsmRegistrationInfo(&result._1, &result._2, &result._3, error);
  // We don't always get an SDK callback when the network technology
  // changes, so simulate a callback here to make sure that the
  // most up-to-date idea of network technology gets signaled.
  PostCallbackRequest(CheckDataCapabilities, new CallbackArgs());
  return result;
}

void GobiGsmModem::SetAllowedMode(const uint32_t& mode,
                                  DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SetAllowedMmode not implemented";
}

//======================================================================
// DBUS Methods: Modem.Gsm.Card

std::string GobiGsmModem::GetImei(DBus::Error& error) {
  SerialNumbers serials;
  bool was_connected = IsApiConnected();
  if (!was_connected)
    ApiConnect(error);
  if (error.is_set())
    return "";
  GetSerialNumbers(&serials, error);
  if (!was_connected)
    ApiDisconnect();
  return error.is_set() ? "" : serials.imei;
}

std::string GobiGsmModem::GetImsi(DBus::Error& error) {
  char imsi[kDefaultBufferSize];
  bool was_connected = IsApiConnected();
  if (!was_connected)
    ApiConnect(error);
  if (error.is_set())
    return "";
  ULONG rc = sdk_->GetIMSI(kDefaultBufferSize, imsi);
  if (!was_connected)
    ApiDisconnect();
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetIMSI, rc, kSdkError, "");
  return imsi;
}

void GobiGsmModem::SendPuk(const std::string& puk,
                           const std::string& pin,
                           DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SendPuk not implemented";
}

void GobiGsmModem::SendPin(const std::string& pin, DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::SendPin not implemented";
}

void GobiGsmModem::EnablePin(const std::string& pin,
                             const bool& enabled,
                             DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::EnablePin not implemented";
}

void GobiGsmModem::ChangePin(const std::string& old_pin,
                             const std::string& new_pin,
                             DBus::Error& error) {
  LOG(WARNING) << "GobiGsmModem::ChangePin not implemented";
}

std::string GobiGsmModem::GetOperatorId(DBus::Error& error) {
  std::string result;
  WORD mcc, mnc, sid, nid;
  CHAR netname[32];

  ULONG rc = sdk_->GetHomeNetwork(&mcc, &mnc,
                                  sizeof(netname), netname, &sid, &nid);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetHomeNetwork, rc, kSdkError, result);
  return MakeOperatorCode(mcc, mnc);
}

//======================================================================
// DBUS Methods: Modem.Gsm.SMS

void GobiGsmModem::Delete(const uint32_t& index, DBus::Error &error) {
  ULONG lindex = index;
  ULONG rc = sdk_->DeleteSMS(gobi::kSmsNonVolatileMemory, &lindex, NULL);
  ENSURE_SDK_SUCCESS(DeleteSMS, rc, kSdkError);
}

utilities::DBusPropertyMap GobiGsmModem::Get(const uint32_t& index,
                                             DBus::Error &error) {
  ULONG tag, format, size;
  BYTE message[400];
  utilities::DBusPropertyMap result;

  size = sizeof(message);
  ULONG rc = sdk_->GetSMS(gobi::kSmsNonVolatileMemory, index,
                          &tag, &format, &size, message);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetSMS, rc, kSdkError, result);
  LOG(INFO) << "GetSms: " << "tag " << tag << " format " << format
            << " size " << size;

  scoped_ptr<SmsMessage> sms(SmsMessage::CreateMessage(message, size));

  if (sms.get() != NULL) {
    result["number"].writer().append_string(sms->sender_address().c_str());
    result["smsc"].writer().append_string(sms->smsc_address().c_str());
    result["text"].writer().append_string(sms->text().c_str());
    result["timestamp"].writer().append_string(sms->timestamp().c_str());
  }
  return result;
}

std::string GobiGsmModem::GetSmsc(DBus::Error &error) {
  CHAR address[100];
  CHAR address_type[100];

  std::string result;
  ULONG rc = sdk_->GetSMSCAddress(sizeof(address), address,
                                  sizeof(address_type), address_type);
  ENSURE_SDK_SUCCESS_WITH_RESULT(GetSMSCAddress, rc, kSdkError, result);
  LOG(INFO) << "SMSC address: " << address << " type: " << address_type;
  result = address;
  return result;
}

void GobiGsmModem::SetSmsc(const std::string& smsc, DBus::Error &error) {
  CHAR *addr = const_cast<CHAR*>(smsc.c_str());
  ULONG rc = sdk_->SetSMSCAddress(addr, NULL);
  ENSURE_SDK_SUCCESS(GetSMSCAddress, rc, kSdkError);
}

std::vector<utilities::DBusPropertyMap> GobiGsmModem::List(DBus::Error &error) {
  std::vector<utilities::DBusPropertyMap> result;
  LOG(WARNING) << "GobiGsmModem::List not implemented";
  return result;
}

std::vector<uint32_t> GobiGsmModem::Save(
    const utilities::DBusPropertyMap& properties,
    DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::Save not implemented";
  return std::vector<uint32_t>();
}

std::vector<uint32_t> GobiGsmModem::Send(
    const utilities::DBusPropertyMap& properties,
    DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::Send not implemented";
  return std::vector<uint32_t>();
}

void GobiGsmModem::SendFromStorage(const uint32_t& index, DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::SendFromStorage not implemented";
}

// What is this supposed to do?
void GobiGsmModem::SetIndication(const uint32_t& mode,
                                 const uint32_t& mt,
                                 const uint32_t& bm,
                                 const uint32_t& ds,
                                 const uint32_t& bfr,
                                 DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::SetIndication not implemented";
}

// The API documentation says nothing about what this is supposed
// to return. Most likely it's intended to report whether messages
// are being sent and received in text mode or PDU mode. But the
// meanings of the return values are undocumented.
uint32_t GobiGsmModem::GetFormat(DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::GetFormat not implemented";
  return 0;
}

// The API documentation says nothing about what this is supposed
// to return. Most likely it's intended for specifying whether messages
// are being sent and received in text mode or PDU mode. But the
// meanings of the argument values are undocumented.
void GobiGsmModem::SetFormat(const uint32_t& format, DBus::Error &error) {
  LOG(WARNING) << "GobiGsmModem::SetFormat not implemented";
}
