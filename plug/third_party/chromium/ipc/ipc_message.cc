// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/ipc_message.h"

#include "base/atomic_sequence_num.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "ipc/ipc_message_attachment.h"
#include "ipc/ipc_message_attachment_set.h"
#include "ipc/placeholder_brokerable_attachment.h"

#if defined(OS_POSIX)
#include "base/file_descriptor_posix.h"
#include "ipc/ipc_platform_file_attachment_posix.h"
#endif

namespace {

base::StaticAtomicSequenceNumber g_ref_num;

// Create a reference number for identifying IPC messages in traces. The return
// values has the reference number stored in the upper 24 bits, leaving the low
// 8 bits set to 0 for use as flags.
inline uint32 GetRefNumUpper24() {
  base::trace_event::TraceLog* trace_log =
      base::trace_event::TraceLog::GetInstance();
  uint32 pid = trace_log ? trace_log->process_id() : 0;
  uint32 count = g_ref_num.GetNext();
  // The 24 bit hash is composed of 14 bits of the count and 10 bits of the
  // Process ID. With the current trace event buffer cap, the 14-bit count did
  // not appear to wrap during a trace. Note that it is not a big deal if
  // collisions occur, as this is only used for debugging and trace analysis.
  return ((pid << 14) | (count & 0x3fff)) << 8;
}

}  // namespace

namespace IPC {

//------------------------------------------------------------------------------

Message::~Message() {
}

Message::Message() : base::Pickle(sizeof(Header)) {
  header()->routing = header()->type = 0;
  header()->flags = GetRefNumUpper24();
#if USE_ATTACHMENT_BROKER
  header()->num_brokered_attachments = 0;
#endif
#if defined(OS_POSIX)
  header()->num_fds = 0;
  header()->pad = 0;
#endif
  Init();
}

Message::Message(int32 routing_id, uint32 type, PriorityValue priority)
    : base::Pickle(sizeof(Header)) {
  header()->routing = routing_id;
  header()->type = type;
  DCHECK((priority & 0xffffff00) == 0);
  header()->flags = priority | GetRefNumUpper24();
#if USE_ATTACHMENT_BROKER
  header()->num_brokered_attachments = 0;
#endif
#if defined(OS_POSIX)
  header()->num_fds = 0;
  header()->pad = 0;
#endif
  Init();
}

Message::Message(const char* data, int data_len)
    : base::Pickle(data, data_len) {
  Init();
}

Message::Message(const Message& other) : base::Pickle(other) {
  Init();
  attachment_set_ = other.attachment_set_;
}

void Message::Init() {
  dispatch_error_ = false;
  sender_pid_ = base::kNullProcessId;
#ifdef IPC_MESSAGE_LOG_ENABLED
  received_time_ = 0;
  dont_log_ = false;
  log_data_ = NULL;
#endif
}

Message& Message::operator=(const Message& other) {
  *static_cast<base::Pickle*>(this) = other;
  attachment_set_ = other.attachment_set_;
  return *this;
}

void Message::SetHeaderValues(int32 routing, uint32 type, uint32 flags) {
  // This should only be called when the message is already empty.
  DCHECK(payload_size() == 0);

  header()->routing = routing;
  header()->type = type;
  header()->flags = flags;
}

void Message::EnsureMessageAttachmentSet() {
  if (attachment_set_.get() == NULL)
    attachment_set_ = new MessageAttachmentSet;
}

#ifdef IPC_MESSAGE_LOG_ENABLED
void Message::set_sent_time(int64 time) {
  DCHECK((header()->flags & HAS_SENT_TIME_BIT) == 0);
  header()->flags |= HAS_SENT_TIME_BIT;
  WriteInt64(time);
}

int64 Message::sent_time() const {
  if ((header()->flags & HAS_SENT_TIME_BIT) == 0)
    return 0;

  const char* data = end_of_payload();
  data -= sizeof(int64);
  return *(reinterpret_cast<const int64*>(data));
}

void Message::set_received_time(int64 time) const {
  received_time_ = time;
}
#endif

Message::NextMessageInfo::NextMessageInfo()
    : message_found(false), pickle_end(nullptr), message_end(nullptr) {}
Message::NextMessageInfo::~NextMessageInfo() {}

// static
Message::NextMessageInfo Message::FindNext(const char* range_start,
                                           const char* range_end) {
  NextMessageInfo info;
  const char* pickle_end =
      base::Pickle::FindNext(sizeof(Header), range_start, range_end);
  if (pickle_end == nullptr)
    return info;
  info.pickle_end = pickle_end;

#if USE_ATTACHMENT_BROKER
  // The data is not copied.
  size_t pickle_len = static_cast<size_t>(pickle_end - range_start);
  Message message(range_start, static_cast<int>(pickle_len));
  int num_attachments = message.header()->num_brokered_attachments;

  // Each brokered attachment adds kNonceSize bytes of data to the message. We
  // want to avoid overflows in our computations, so we limit the number of
  // brokerable attachments to 100.
  if (num_attachments > 100)
    return info;

  // Check whether the range includes the attachments.
  size_t attachment_length = num_attachments * BrokerableAttachment::kNonceSize;
  size_t buffer_length = static_cast<size_t>(range_end - range_start);
  if (buffer_length < attachment_length + pickle_len)
    return info;

  for (int i = 0; i < num_attachments; ++i) {
    const char* attachment_start =
        pickle_end + i * (BrokerableAttachment::kNonceSize);
    BrokerableAttachment::AttachmentId id(attachment_start,
                                          BrokerableAttachment::kNonceSize);
    info.attachment_ids.push_back(id);
  }
  info.message_end =
      pickle_end + num_attachments * BrokerableAttachment::kNonceSize;
#else
  info.message_end = pickle_end;
#endif  // USE_ATTACHMENT_BROKER

  info.message_found = true;
  return info;
}

Message::SerializedAttachmentIds
Message::SerializedIdsOfBrokerableAttachments() {
  DCHECK(HasBrokerableAttachments());
  std::vector<const BrokerableAttachment*> attachments =
      attachment_set_->PeekBrokerableAttachments();
  size_t size = attachments.size() * BrokerableAttachment::kNonceSize;
  char* buffer = static_cast<char*>(malloc(size));
  for (size_t i = 0; i < attachments.size(); ++i) {
    const BrokerableAttachment* attachment = attachments[i];
    char* start_range = buffer + i * BrokerableAttachment::kNonceSize;
    BrokerableAttachment::AttachmentId id = attachment->GetIdentifier();
    id.SerializeToBuffer(start_range, BrokerableAttachment::kNonceSize);
  }
  SerializedAttachmentIds ids;
  ids.buffer = buffer;
  ids.size = size;
  return ids;
}

bool Message::AddPlaceholderBrokerableAttachmentWithId(
    BrokerableAttachment::AttachmentId id) {
  scoped_refptr<PlaceholderBrokerableAttachment> attachment(
      new PlaceholderBrokerableAttachment(id));
  return attachment_set()->AddAttachment(attachment);
}

bool Message::WriteAttachment(scoped_refptr<MessageAttachment> attachment) {
  // We write the index of the descriptor so that we don't have to
  // keep the current descriptor as extra decoding state when deserialising.
  WriteInt(attachment_set()->size());

#if USE_ATTACHMENT_BROKER
  if (attachment->GetType() == MessageAttachment::TYPE_BROKERABLE_ATTACHMENT)
    header()->num_brokered_attachments += 1;
#endif

  return attachment_set()->AddAttachment(attachment);
}

bool Message::ReadAttachment(
    base::PickleIterator* iter,
    scoped_refptr<MessageAttachment>* attachment) const {
  int descriptor_index;
  if (!iter->ReadInt(&descriptor_index))
    return false;

  MessageAttachmentSet* attachment_set = attachment_set_.get();
  if (!attachment_set)
    return false;

  *attachment = attachment_set->GetAttachmentAt(descriptor_index);
  return nullptr != attachment->get();
}

bool Message::HasAttachments() const {
  return attachment_set_.get() && !attachment_set_->empty();
}

bool Message::HasMojoHandles() const {
  return attachment_set_.get() && attachment_set_->num_mojo_handles() > 0;
}

bool Message::HasBrokerableAttachments() const {
  return attachment_set_.get() &&
         attachment_set_->num_brokerable_attachments() > 0;
}

}  // namespace IPC