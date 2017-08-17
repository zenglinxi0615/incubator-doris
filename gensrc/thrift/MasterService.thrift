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

namespace cpp palo
namespace java com.baidu.palo.thrift

include "AgentService.thrift"
include "PaloInternalService.thrift"
include "Types.thrift"
include "Status.thrift"

struct TTabletInfo {
    1: required Types.TTabletId tablet_id
    2: required Types.TSchemaHash schema_hash
    3: required Types.TVersion version
    4: required Types.TVersionHash version_hash
    5: required Types.TCount row_count
    6: required Types.TSize data_size
    7: optional Types.TStorageMedium storage_medium
}

struct TFinishTaskRequest {
    1: required Types.TBackend backend
    2: required Types.TTaskType task_type
    3: required i64 signature
    4: required Status.TStatus task_status
    5: optional i64 report_version
    6: optional list<TTabletInfo> finish_tablet_infos
    7: optional i64 tablet_checksum
    8: optional i64 request_version
    9: optional i64 request_version_hash
    10: optional string snapshot_path
}

struct TTablet {
    1: required list<TTabletInfo> tablet_infos
}

struct TDisk {
    1: required string root_path
    2: required Types.TSize total_capacity
    3: required Types.TSize available_capacity
    4: required bool used
}

struct TReportRequest {
    1: required Types.TBackend backend
    2: optional i64 report_version
    3: optional map<Types.TTaskType, set<i64>> tasks // string signature
    4: optional map<Types.TTabletId, TTablet> tablets
    5: optional map<string, TDisk> disks // string root_path
}

struct TMasterResult {
    // required in V1
    1: required Status.TStatus status
}

// Now we only support CPU share.
enum TResourceType {
    TRESOURCE_CPU_SHARE
    TRESOURCE_IO_SHARE
    TRESOURCE_SSD_READ_IOPS
    TRESOURCE_SSD_WRITE_IOPS
    TRESOURCE_SSD_READ_MBPS
    TRESOURCE_SSD_WRITE_MBPS
    TRESOURCE_HDD_READ_IOPS
    TRESOURCE_HDD_WRITE_IOPS
    TRESOURCE_HDD_READ_MBPS
    TRESOURCE_HDD_WRITE_MBPS
}

struct TResourceGroup {
    1: required map<TResourceType, i32> resourceByType
}

// Resource per user
struct TUserResource {
    1: required TResourceGroup resource

    // Share in this user quota; Now only support High, Normal, Low
    2: required map<string, i32> shareByGroup
}

struct TFetchResourceResult {
    // Master service not find protocol version, so using agent service version
    1: required AgentService.TAgentServiceVersion protocolVersion
    2: required i64 resourceVersion
    3: required map<string, TUserResource> resourceByUser
}