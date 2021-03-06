#include "src/s3_cmds/zgw_s3_object.h"

#include <glog/logging.h>
#include "slash/include/env.h"
#include "src/zgwstore/zgw_define.h"

bool PutObjectCmd::DoInitial() {
  http_response_xml_.clear();
  md5_ctx_.Init();
  status_ = Status::OK();
  block_start_ = 0;
  block_end_ = 0;

  request_id_ = md5(bucket_name_ +
                    object_name_ +
                    std::to_string(slash::NowMicros()));

  size_t data_size = std::stoul(req_headers_["content-length"]);
  size_t m = data_size % zgwstore::kZgwBlockSize;
  block_count_ = data_size / zgwstore::kZgwBlockSize + (m > 0 ? 1 : 0);

  if (!TryAuth()) {
    DLOG(INFO) << request_id_ << " " <<
      "PutObject(DoInitial) - Auth failed: " << client_ip_port_;
    g_zgw_monitor->AddAuthFailed();
    return false;
  }

  DLOG(INFO) << request_id_ << " " <<
    "PutObject(DoInitial) - " << bucket_name_ << "/" << object_name_;

  // Initial new_object_
  new_object_.bucket_name = bucket_name_;
  new_object_.object_name = object_name_;
  new_object_.etag = ""; // Postpone
  new_object_.size = data_size;
  new_object_.owner = user_name_;
  new_object_.last_modified = 0; // Postpone
  new_object_.storage_class = 0; // Unused
  new_object_.acl = "FULL_CONTROL";
  new_object_.upload_id = "_"; // Doesn't need
  new_object_.data_block = ""; // Postpone

  std::string data_blocks;
  Status s = store_->AllocateId(user_name_, bucket_name_, object_name_,
                                block_count_, &block_end_);
  if (s.ok()) {
    block_start_ = block_end_ - block_count_;
    http_ret_code_ = 200;
    char buf[100];
    sprintf(buf, "%lu-%lu(0,%lu)", block_start_, block_end_ - 1, data_size);
    new_object_.data_block = std::string(buf);
    DLOG(INFO) << request_id_ << " " <<
      "PutObject(DoInitial) - " << bucket_name_ << "/" <<
      object_name_ << "AllocateId: " << block_start_ << "-" << block_end_ - 1;
  } else if (s.ToString().find("Bucket NOT Exists") != std::string::npos ||
             s.ToString().find("Bucket Doesn't Belong To This User") !=
             std::string::npos) {
    http_ret_code_ = 404;
    GenerateErrorXml(kNoSuchBucket, bucket_name_);
    return false;
  } else {
    http_ret_code_ = 500;
    LOG(ERROR) << request_id_ << " " <<
      "PutObject(DoInitial) - AllocateId " << bucket_name_ << "/" <<
      object_name_ << "error" << s.ToString();
    return false;
  }

  return true;
}

// Data size from HTTP is 8MB per invocation, or smaller as the last
void PutObjectCmd::DoReceiveBody(const char* data, size_t data_size) {
  if (http_ret_code_ != 200) {
    return;
  }

  char* buf_pos = const_cast<char*>(data);
  size_t remain_size = data_size;
  DLOG(INFO) << request_id_ << " " <<
    bucket_name_ << "/" << object_name_ << " Remain size: " << remain_size;

  while (remain_size > 0) {
    if (block_start_ >= block_end_) {
      // LOG WARNING
      LOG(WARNING) << request_id_ << " " <<
        "PutObject Block error, block_start_: " << block_start_ <<
        " block_end_: " << block_end_;
      return;
    }
    size_t nwritten = std::min(remain_size, zgwstore::kZgwBlockSize);
    status_ = store_->BlockSet(std::to_string(block_start_++),
                               std::string(buf_pos, nwritten));
    if (status_.ok()) {
      md5_ctx_.Update(buf_pos, nwritten);
      g_zgw_monitor->AddBucketTraffic(bucket_name_, nwritten);
    } else {
      http_ret_code_ = 500;
      LOG(ERROR) << request_id_ << " " <<
        "PutObject(DoReceiveBody) - BlockSet: " << block_start_ - 1 << " error";
    }

    remain_size -= nwritten;
    buf_pos += nwritten;
  }
}

void PutObjectCmd::DoAndResponse(pink::HTTPResponse* resp) {
  if (http_ret_code_ == 200) {
    if (!status_.ok()) {
      http_ret_code_ = 500;
      // Error happend while transmiting to zeppelin
      LOG(ERROR) << request_id_ << " " <<
        "PutObject(DoAndResponse) - writing to zp error" << status_.ToString();
    } else {
      // Write meta
      new_object_.etag = md5_ctx_.ToString();
      LOG(INFO) << "MD5: " << new_object_.etag;
      if (new_object_.etag.empty()) {
        new_object_.etag = "_";
      }
      new_object_.last_modified = slash::NowMicros();

      status_ = store_->AddObject(new_object_);
      if (!status_.ok()) {
        http_ret_code_ = 500;
        LOG(ERROR) << request_id_ << " " <<
          "PutObject(DoAndResponse) - AddObject error" << status_.ToString();
      }
      DLOG(INFO) << "AddObject: " << bucket_name_ + "/" + object_name_ + " Success";
    }
    resp->SetHeaders("Last-Modified", http_nowtime(new_object_.last_modified));
    resp->SetHeaders("ETag", "\"" + new_object_.etag + "\"");
  }

  g_zgw_monitor->AddApiRequest(kPutObject, http_ret_code_);
  resp->SetStatusCode(http_ret_code_);
  resp->SetContentLength(http_response_xml_.size());
}

int PutObjectCmd::DoResponseBody(char* buf, size_t max_size) {
  if (max_size < http_response_xml_.size()) {
    memcpy(buf, http_response_xml_.data(), max_size);
    http_response_xml_.assign(http_response_xml_.substr(max_size));
  } else {
    memcpy(buf, http_response_xml_.data(), http_response_xml_.size());
  }

  return std::min(max_size, http_response_xml_.size());
}
