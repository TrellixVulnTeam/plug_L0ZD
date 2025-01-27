// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_BROKERABLE_ATTACHMENT_H_
#define IPC_BROKERABLE_ATTACHMENT_H_

#include <stdint.h>

#include "base/macros.h"
#include "ipc/ipc_export.h"
#include "ipc/ipc_message_attachment.h"

namespace IPC {

// This subclass of MessageAttachment requires an AttachmentBroker to be
// attached to a Chrome IPC message.
class IPC_EXPORT BrokerableAttachment : public MessageAttachment {
 public:
  static const size_t kNonceSize = 16;
  // An id uniquely identifies an attachment sent via a broker.
  struct IPC_EXPORT AttachmentId {
    uint8_t nonce[kNonceSize];

    // Default constructor returns a random nonce.
    AttachmentId();

    // Constructs an AttachmentId from a buffer.
    AttachmentId(const char* start_address, size_t size);

    // Writes the nonce into a buffer.
    void SerializeToBuffer(char* start_address, size_t size);

    bool operator==(const AttachmentId& rhs) const {
      for (size_t i = 0; i < kNonceSize; ++i) {
        if (nonce[i] != rhs.nonce[i])
          return false;
      }
      return true;
    }

    bool operator<(const AttachmentId& rhs) const {
      for (size_t i = 0; i < kNonceSize; ++i) {
        if (nonce[i] < rhs.nonce[i])
          return true;
        if (nonce[i] > rhs.nonce[i])
          return false;
      }
      return false;
    }
  };

  enum BrokerableType {
    PLACEHOLDER,
    WIN_HANDLE,
  };

  // The identifier is unique across all Chrome processes.
  AttachmentId GetIdentifier() const;

  // Whether the attachment still needs information from the broker before it
  // can be used.
  bool NeedsBrokering() const;

  // Returns TYPE_BROKERABLE_ATTACHMENT
  Type GetType() const override;

  virtual BrokerableType GetBrokerableType() const = 0;

// MessageAttachment override.
#if defined(OS_POSIX)
  base::PlatformFile TakePlatformFile() override;
#endif  // OS_POSIX

 protected:
  BrokerableAttachment();
  BrokerableAttachment(const AttachmentId& id);
  ~BrokerableAttachment() override;

 private:
  // This member uniquely identifies a BrokerableAttachment across all Chrome
  // processes.
  const AttachmentId id_;

  DISALLOW_COPY_AND_ASSIGN(BrokerableAttachment);
};

}  // namespace IPC

#endif  // IPC_BROKERABLE_ATTACHMENT_H_
