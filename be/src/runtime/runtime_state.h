// Modifications copyright (C) 2017, Baidu.com, Inc.
// Copyright 2017 The Apache Software Foundation

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#ifndef BDG_PALO_BE_SRC_QUERY_RUNTIME_RUNTIME_STATE_H
#define BDG_PALO_BE_SRC_QUERY_RUNTIME_RUNTIME_STATE_H

#include "common/object_pool.h"

#include <boost/scoped_ptr.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>

#include <atomic>
#include <vector>
#include <string>
// stringstream is a typedef, so can't forward declare it.
#include <sstream>
#include <memory>

#include "runtime/exec_env.h"
#include "util/logging.h"
#include "runtime/descriptors.h"  // for PlanNodeId
#include "runtime/mem_pool.h"
#include "runtime/thread_resource_mgr.h"
#include "gen_cpp/Types_types.h"  // for TUniqueId
#include "gen_cpp/PaloInternalService_types.h"  // for TQueryOptions
#include "util/runtime_profile.h"
#include "runtime/buffered_block_mgr.h"
#include "runtime/buffered_block_mgr2.h"

namespace palo {

class DescriptorTbl;
class ObjectPool;
class Status;
class ExecEnv;
class Expr;
class LlvmCodeGen;
class DateTimeValue;
class MemTracker;
class DataStreamRecvr;
class ResultBufferMgr;
class ThreadPool;
class DiskIoMgrs;
class TmpFileMgr;
class BufferedBlockMgr;
class BufferedBlockMgr2;
class LoadErrorHub;

// A collection of items that are part of the global state of a
// query and shared across all execution nodes of that query.
class RuntimeState {
public:
    RuntimeState(const TUniqueId& fragment_instance_id,
                 const TQueryOptions& query_options,
                 const std::string& now, ExecEnv* exec_env);

    RuntimeState(
        const TExecPlanFragmentParams& fragment_params,
        const TQueryOptions& query_options,
        const std::string& now, ExecEnv* exec_env);

    // RuntimeState for executing expr in fe-support.
    RuntimeState(const std::string& now);

    // Empty d'tor to avoid issues with scoped_ptr.
    ~RuntimeState();

    // Set per-query state.
    Status init(const TUniqueId& fragment_instance_id,
                const TQueryOptions& query_options,
                const std::string& now, ExecEnv* exec_env);

    // Set up four-level hierarchy of mem trackers: process, query, fragment instance.
    // The instance tracker is tied to our profile.
    // Specific parts of the fragment (i.e. exec nodes, sinks, data stream senders, etc)
    // will add a fourth level when they are initialized.
    // This function also initializes a user function mem tracker (in the fourth level).
    Status init_mem_trackers(const TUniqueId& query_id);

    // Gets/Creates the query wide block mgr.
    Status create_block_mgr();

    Status create_load_dir();

    const TQueryOptions& query_options() const {
        return _query_options;
    }
    ObjectPool* obj_pool() const {
        return _obj_pool.get();
    }

    std::shared_ptr<ObjectPool> obj_pool_ptr() const {
        return _obj_pool;
    }

    const DescriptorTbl& desc_tbl() const {
        return *_desc_tbl;
    }
    void set_desc_tbl(DescriptorTbl* desc_tbl) {
        _desc_tbl = desc_tbl;
    }
    int batch_size() const {
        return _query_options.batch_size;
    }
    bool abort_on_error() const {
        return _query_options.abort_on_error;
    }
    bool abort_on_default_limit_exceeded() const {
        return _query_options.abort_on_default_limit_exceeded;
    }
    int max_errors() const {
        return _query_options.max_errors;
    }
    int max_io_buffers() const {
        return _query_options.max_io_buffers;
    }
    int num_scanner_threads() const {
        return _query_options.num_scanner_threads;
    }
    const DateTimeValue* now() const {
        return _now.get();
    }
    const std::string& user() const {
        return _user;
    }
    const std::vector<std::string>& error_log() const {
        return _error_log;
    }
    const TUniqueId& query_id() const {
        return _query_id;
    }
    const TUniqueId& fragment_instance_id() const {
        return _fragment_instance_id;
    }
    ExecEnv* exec_env() {
        return _exec_env;
    }
    DataStreamMgr* stream_mgr() {
        return _exec_env->stream_mgr();
    }
    ResultBufferMgr* result_mgr() {
        return _exec_env->result_mgr();
    }
    BackendServiceClientCache* client_cache() {
        return _exec_env->client_cache();
    }
    FrontendServiceClientCache* frontend_client_cache() {
        return _exec_env->frontend_client_cache();
    }
    DiskIoMgr* io_mgr() {
        return _exec_env->disk_io_mgr();
    }
    std::vector<MemTracker*>* mem_trackers() {
        return &_mem_trackers;
    }
    MemTracker* fragment_mem_tracker() {
        return _fragment_mem_tracker;
    }
    MemTracker* instance_mem_tracker() { {
        return _instance_mem_tracker.get(); }
    }
    MemTracker* query_mem_tracker() { {
        return _query_mem_tracker.get(); }
    }
    ThreadResourceMgr::ResourcePool* resource_pool() {
        return _resource_pool;
    }

    void set_fragment_root_id(PlanNodeId id) {
        DCHECK(_root_node_id == -1) << "Should not set this twice.";
        _root_node_id = id;
    }

    // The seed value to use when hashing tuples.
    // See comment on _root_node_id. We add one to prevent having a hash seed of 0.
    uint32_t fragment_hash_seed() const {
        return _root_node_id + 1;
    }

    ThreadPool* etl_thread_pool() {
        return _exec_env->etl_thread_pool();
    }

    // Returns true if the codegen object has been created. Note that this may return false
    // even when codegen is enabled if nothing has been codegen'd.
    bool codegen_created() const {
        return _codegen.get() != NULL;
    }

    // Returns runtime state profile
    RuntimeProfile* runtime_profile() {
        return &_profile;
    }

    // Returns true if codegen is enabled for this query.
    bool codegen_enabled() const {
        return !_query_options.disable_codegen;
    }

    // Returns CodeGen object.  Returns NULL if codegen is disabled.
    LlvmCodeGen* llvm_codegen() {
        return _codegen.get();
    }
    // Returns CodeGen object.  Returns NULL if the codegen object has not been
    // created. If codegen is enabled for the query, the codegen object will be
    // created as part of the RuntimeState's initialization.
    // Otherwise, it can be created by calling create_codegen().
    LlvmCodeGen* codegen() {
        return _codegen.get();
    }

    // Create a codegen object in _codegen. No-op if it has already been called.
    // If codegen is enabled for the query, this is created when the runtime
    // state is created. If codegen is disabled for the query, this is created
    // on first use.
    Status create_codegen();

    BufferedBlockMgr* block_mgr() {
        DCHECK(_block_mgr.get() != NULL);
        return _block_mgr.get();
    }
    BufferedBlockMgr2* block_mgr2() {
        DCHECK(_block_mgr2.get() != NULL);
        return _block_mgr2.get();
    }

    Status query_status() {
        boost::lock_guard<boost::mutex> l(_process_status_lock);
        return _process_status;
    };

    MemPool* udf_pool() {
        return _udf_pool.get();
    };

    // Create and return a stream receiver for _fragment_instance_id
    // from the data stream manager. The receiver is added to _data_stream_recvrs_pool.
    DataStreamRecvr* create_recvr(
        const RowDescriptor& row_desc, PlanNodeId dest_node_id, int num_senders,
        int buffer_size, RuntimeProfile* profile);

    // Sets the fragment memory limit and adds it to _mem_trackers
    void set_fragment_mem_tracker(MemTracker* limit) {
        DCHECK(_fragment_mem_tracker == NULL);
        _fragment_mem_tracker = limit;
        _mem_trackers.push_back(limit);
    }

    // Appends error to the _error_log if there is space
    bool log_error(const std::string& error);

    // If !status.ok(), appends the error to the _error_log
    void log_error(const Status& status);

    // Returns true if the error log has not reached _max_errors.
    bool log_has_space() {
        boost::lock_guard<boost::mutex> l(_error_log_lock);
        return _error_log.size() < _query_options.max_errors;
    }

    // Return true if error log is empty.
    bool error_log_is_empty();

    // Returns the error log lines as a string joined with '\n'.
    std::string error_log();

    // Append all _error_log[_unreported_error_idx+] to new_errors and set
    // _unreported_error_idx to _errors_log.size()
    void get_unreported_errors(std::vector<std::string>* new_errors);

    // Returns _codegen in 'codegen'. If 'initialize' is true, _codegen will be created if
    // it has not been initialized by a previous call already. If 'initialize' is false,
    // 'codegen' will be set to NULL if _codegen has not been initialized.
    Status get_codegen(LlvmCodeGen** codegen, bool initialize);
    Status get_codegen(LlvmCodeGen** codegen);

    bool is_cancelled() const {
        return _is_cancelled;
    }
    int codegen_level() const {
        return _query_options.codegen_level;
    }
    void set_is_cancelled(bool v) {
        _is_cancelled = v;
    }

    void set_be_number(int be_number) {
        _be_number = be_number;
    }
    int be_number(void) {
        return _be_number;
    }

    // Sets _process_status with err_msg if no error has been set yet.
    void set_process_status(const std::string& err_msg) {
        boost::lock_guard<boost::mutex> l(_process_status_lock);

        if (!_process_status.ok()) {
            return;
        }

        _process_status = Status(err_msg);
    }

    void set_process_status(const Status& status) {
        if (status.ok()) {
            return;
        }
        boost::lock_guard<boost::mutex> l(_process_status_lock);
        if (!_process_status.ok()) {
            return;
        }
        _process_status = status;
    }

    // Sets query_status_ to MEM_LIMIT_EXCEEDED and logs all the registered trackers.
    // Subsequent calls to this will be no-ops. Returns query_status_.
    // If 'failed_allocation_size' is not 0, then it is the size of the allocation (in
    // bytes) that would have exceeded the limit allocated for 'tracker'.
    // This value and tracker are only used for error reporting.
    // If 'msg' is non-NULL, it will be appended to query_status_ in addition to the
    // generic "Memory limit exceeded" error.
    Status set_mem_limit_exceeded(
            MemTracker* tracker = NULL,
            int64_t failed_allocation_size = 0,
            const std::string* msg = NULL);

    Status set_mem_limit_exceeded(const std::string& msg) {
        return set_mem_limit_exceeded(NULL, 0, &msg);
    }

    // Returns a non-OK status if query execution should stop (e.g., the query was cancelled
    // or a mem limit was exceeded). Exec nodes should check this periodically so execution
    // doesn't continue if the query terminates abnormally.
    Status check_query_state();

    std::vector<std::string>& output_files() {
        return _output_files;
    }

    void set_import_label(const std::string& import_label) {
        _import_label = import_label;
    }

    const std::string& import_label() {
        return _import_label;
    }

    const std::vector<std::string>& export_output_files() const {
        return _export_output_files;
    }

    void add_export_output_file(const std::string& file) {
        _export_output_files.push_back(file);
    }

    void set_db_name(const std::string& db_name) {
        _db_name = db_name;
    }

    const std::string& db_name() {
        return _db_name;
    }

    const std::string& load_dir() const {
        return _load_dir;
    }

    const void set_load_dir(std::string& dir) {
        _load_dir = dir;
    }

    void set_load_job_id(int64_t job_id) {
        _load_job_id = job_id;
    }

    const int64_t load_job_id() {
        return _load_job_id;
    }

    // we only initialize object for load jobs
    void set_load_error_hub_info(const TLoadErrorHubInfo& hub_info) {
        TLoadErrorHubInfo* info = new TLoadErrorHubInfo(hub_info);
        _load_error_hub_info.reset(info);
    }

    // only can be invoded after set its value
    const TLoadErrorHubInfo* load_error_hub_info() {
        // DCHECK(_load_error_hub_info != nullptr);
        return _load_error_hub_info.get();
    }

    const int64_t get_normal_row_number() const {
        return _normal_row_number;
    }

    const void set_normal_row_number(int64_t number) {
        _normal_row_number = number;
    }

    const int64_t get_error_row_number() const {
        return _error_row_number;
    }

    const void set_error_row_number(int64_t number) {
        _error_row_number = number;
    }

    const std::string get_error_log_file_path() const {
        return _error_log_file_path;
    }

    const void set_error_log_file_path(const std::string& file_path) {
        _error_log_file_path = file_path;
    }

    // TODO(lingbin): remove this file error method after mysql error exporter is stable.
    void append_error_msg_to_file(const std::string& line, const std::string& error_msg);

    int64_t num_rows_load_success() {
        return _num_rows_load_success.load();
    }

    int64_t num_rows_load_filtered() {
        return _num_rows_load_filtered.load();
    }

    void update_num_rows_load_success(int64_t num_rows) {
        _num_rows_load_success.fetch_add(num_rows);
    }

    void update_num_rows_load_filtered(int64_t num_rows) {
        _num_rows_load_filtered.fetch_add(num_rows);
    }
    void export_load_error(const std::string& error_msg);

    void set_per_fragment_instance_idx(int idx) {
        _per_fragment_instance_idx = idx;
    }

    int per_fragment_instance_idx() const {
        return _per_fragment_instance_idx;
    }

private:
    // Allow TestEnv to set block_mgr manually for testing.
    friend class TestEnv;

    // Use a custom block manager for the query for testing purposes.
    void set_block_mgr(const boost::shared_ptr<BufferedBlockMgr>& block_mgr) {
        _block_mgr = block_mgr;
    }
    // Use a custom block manager for the query for testing purposes.
    void set_block_mgr2(const boost::shared_ptr<BufferedBlockMgr2>& block_mgr) {
        _block_mgr2 = block_mgr;
    }

    Status create_error_log_file();

    static const int DEFAULT_BATCH_SIZE = 1024;

    DescriptorTbl* _desc_tbl;
    std::shared_ptr<ObjectPool> _obj_pool;

    // Protects _data_stream_recvrs_pool
    boost::mutex _data_stream_recvrs_lock;

    // Data stream receivers created by a plan fragment are gathered here to make sure
    // they are destroyed before _obj_pool (class members are destroyed in reverse order).
    // Receivers depend on the descriptor table and we need to guarantee that their control
    // blocks are removed from the data stream manager before the objects in the
    // descriptor table are destroyed.
    boost::scoped_ptr<ObjectPool> _data_stream_recvrs_pool;

    // Lock protecting _error_log and _unreported_error_idx
    boost::mutex _error_log_lock;

    // Logs error messages.
    std::vector<std::string> _error_log;

    // _error_log[_unreported_error_idx+] has been not reported to the coordinator.
    int _unreported_error_idx;

    // Username of user that is executing the query to which this RuntimeState belongs.
    std::string _user;
    // Query-global timestamp, e.g., for implementing now().
    // Use pointer to avoid inclusion of timestampvalue.h and avoid clang issues.
    boost::scoped_ptr<DateTimeValue> _now;

    TUniqueId _query_id;
    TUniqueId _fragment_instance_id;
    TQueryOptions _query_options;
    ExecEnv* _exec_env;
    boost::scoped_ptr<LlvmCodeGen> _codegen;

    // Thread resource management object for this fragment's execution.  The runtime
    // state is responsible for returning this pool to the thread mgr.
    ThreadResourceMgr::ResourcePool* _resource_pool;

    RuntimeProfile _profile;

    // all mem limits that apply to this query
    std::vector<MemTracker*> _mem_trackers;

    // Fragment memory limit.  Also contained in _mem_trackers
    MemTracker* _fragment_mem_tracker;

    // MemTracker that is shared by all fragment instances running on this host.
    // The query mem tracker must be released after the _instance_mem_tracker.
    boost::shared_ptr<MemTracker> _query_mem_tracker;

    // Memory usage of this fragment instance
    boost::scoped_ptr<MemTracker> _instance_mem_tracker;

    // if true, execution should stop with a CANCELLED status
    bool _is_cancelled;

    int _per_fragment_instance_idx;

    // used as send id
    int _be_number;

    // Non-OK if an error has occurred and query execution should abort. Used only for
    // asynchronously reporting such errors (e.g., when a UDF reports an error), so this
    // will not necessarily be set in all error cases.
    boost::mutex _process_status_lock;
    Status _process_status;
    boost::scoped_ptr<MemPool> _udf_pool;

    // BufferedBlockMgr object used to allocate and manage blocks of input data in memory
    // with a fixed memory budget.
    // The block mgr is shared by all fragments for this query.
    boost::shared_ptr<BufferedBlockMgr> _block_mgr;
    boost::shared_ptr<BufferedBlockMgr2> _block_mgr2;

    // This is the node id of the root node for this plan fragment. This is used as the
    // hash seed and has two useful properties:
    // 1) It is the same for all exec nodes in a fragment, so the resulting hash values
    // can be shared (i.e. for _slot_bitmap_filters).
    // 2) It is different between different fragments, so we do not run into hash
    // collisions after data partitioning (across fragments). See IMPALA-219 for more
    // details.
    PlanNodeId _root_node_id;

    // put here to collect files??
    std::vector<std::string> _output_files;
    std::atomic<int64_t> _num_rows_load_success;
    std::atomic<int64_t> _num_rows_load_filtered;

    std::vector<std::string> _export_output_files;

    std::string _import_label;
    std::string _db_name;
    std::string _load_dir;
    int64_t _load_job_id;
    std::unique_ptr<TLoadErrorHubInfo> _load_error_hub_info;

    // mini load
    int64_t _normal_row_number;
    int64_t _error_row_number;
    std::string _error_log_file_path;
    std::ofstream* _error_log_file; // error file path, absolute path
    std::unique_ptr<LoadErrorHub> _error_hub;

    // prohibit copies
    RuntimeState(const RuntimeState&);
};

#define RETURN_IF_CANCELLED(state) \
  do { \
    if (UNLIKELY((state)->is_cancelled())) return Status(TStatusCode::CANCELLED); \
  } while (false)

}

#endif // end of BDG_PALO_BE_SRC_QUERY_RUNTIME_RUNTIME_STATE_H