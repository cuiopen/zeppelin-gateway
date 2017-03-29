#ifndef ZGW_CONN_H
#define ZGW_CONN_H

#include "include/pink_thread.h"
#include "include/http_conn.h"

#include "zgw_store.h"

class ZgwWorkerThread;

class ZgwConn : public pink::HttpConn {
 public:
  ZgwConn(const int fd, const std::string &ip_port,
          pink::Thread* worker);

 private:
  virtual void DealMessage(const pink::HttpRequest* req,
                           pink::HttpResponse* res) override;

  // Operation On Objects
  void GetObjectHandle(bool is_head_op = false);
  void PutObjectHandle();
  void DelObjectHandle();
  void DelMultiObjectsHandle();

  void InitialMultiUpload();
  void UploadPartHandle(const std::string& part_num, const std::string& upload_id);
  void ListParts(const std::string& upload_id);
  void CompleteMultiUpload(const std::string& upload_id);
  void AbortMultiUpload(const std::string& upload_id);

  // Operation On Buckets
  void PutBucketHandle();
  void DelBucketHandle();
  void ListObjectHandle();
  void ListMultiPartsUpload();

  // Operation On Service
  void ListBucketHandle();
  void ListUsersHandle();

 private:
  enum METHOD {
    kGet,
    kPut,
    kDelete,
    kHead,
    kPost,
    kUnsupport,
  };

  ZgwWorkerThread *worker_;
  libzgw::ZgwStore *store_;

  // Parse from http request
  std::string access_key_;
  pink::HttpRequest *req_;
  pink::HttpResponse *resp_;
  std::string bucket_name_;
  std::string object_name_;

  // Get from zp
  libzgw::NameList *buckets_name_;
  libzgw::NameList *objects_name_;
  libzgw::ZgwUser *zgw_user_;

  void PreProcessUrl();
  bool IsValidBucket();
  bool IsValidObject();
  bool ParseRange(const std::string& range,
                  std::vector<std::pair<int, uint32_t>>* segments);
  bool GetSourceObject(std::string* content);
};


#endif
