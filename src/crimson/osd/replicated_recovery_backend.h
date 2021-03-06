// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "crimson/osd/recovery_backend.h"

#include "messages/MOSDPGPull.h"
#include "messages/MOSDPGPush.h"
#include "messages/MOSDPGPushReply.h"
#include "messages/MOSDPGRecoveryDelete.h"
#include "messages/MOSDPGRecoveryDeleteReply.h"
#include "os/ObjectStore.h"

class ReplicatedRecoveryBackend : public RecoveryBackend {
public:
  ReplicatedRecoveryBackend(crimson::osd::PG& pg,
			    crimson::osd::ShardServices& shard_services,
			    crimson::os::CollectionRef coll,
			    PGBackend* backend)
    : RecoveryBackend(pg, shard_services, coll, backend) {}
  seastar::future<> handle_recovery_op(
    Ref<MOSDFastDispatchOp> m) final;

  seastar::future<> recover_object(
    const hobject_t& soid,
    eversion_t need) final;
  seastar::future<> recover_delete(
    const hobject_t& soid,
    eversion_t need) final;
  seastar::future<> push_delete(
    const hobject_t& soid,
    eversion_t need) final;
protected:
  seastar::future<> handle_pull(
    Ref<MOSDPGPull> m);
  seastar::future<> handle_pull_response(
    Ref<MOSDPGPush> m);
  seastar::future<> handle_push(
    Ref<MOSDPGPush> m);
  seastar::future<> handle_push_reply(
    Ref<MOSDPGPushReply> m);
  seastar::future<> handle_recovery_delete(
    Ref<MOSDPGRecoveryDelete> m);
  seastar::future<> handle_recovery_delete_reply(
    Ref<MOSDPGRecoveryDeleteReply> m);
  seastar::future<std::map<pg_shard_t, PushOp>> prep_push(
    const hobject_t& soid,
    eversion_t need,
    const std::vector<pg_shard_t>& shards);
  void prepare_pull(
    PullOp& po,
    PullInfo& pi,
    const hobject_t& soid,
    eversion_t need);
  std::vector<pg_shard_t> get_shards_to_push(
    const hobject_t& soid) const;
  seastar::future<PushOp> build_push_op(
    const ObjectRecoveryInfo& recovery_info,
    const ObjectRecoveryProgress& progress,
    object_stat_sum_t* stat);
  seastar::future<bool> _handle_pull_response(
    pg_shard_t from,
    PushOp& pop,
    PullOp* response,
    ceph::os::Transaction* t);
  std::pair<interval_set<uint64_t>, ceph::bufferlist> trim_pushed_data(
    const interval_set<uint64_t> &copy_subset,
    const interval_set<uint64_t> &intervals_received,
    ceph::bufferlist data_received);
  seastar::future<> submit_push_data(
    const ObjectRecoveryInfo &recovery_info,
    bool first,
    bool complete,
    bool clear_omap,
    interval_set<uint64_t> &data_zeros,
    const interval_set<uint64_t> &intervals_included,
    ceph::bufferlist data_included,
    ceph::bufferlist omap_header,
    const std::map<string, bufferlist> &attrs,
    const std::map<string, bufferlist> &omap_entries,
    ceph::os::Transaction *t);
  void submit_push_complete(
    const ObjectRecoveryInfo &recovery_info,
    ObjectStore::Transaction *t);
  seastar::future<> _handle_push(
    pg_shard_t from,
    const PushOp &pop,
    PushReplyOp *response,
    ceph::os::Transaction *t);
  seastar::future<std::optional<PushOp>> _handle_push_reply(
    pg_shard_t peer,
    const PushReplyOp &op);
  seastar::future<> on_local_recover_persist(
    const hobject_t& soid,
    const ObjectRecoveryInfo& _recovery_info,
    bool is_delete,
    epoch_t epoch_to_freeze);
  seastar::future<> local_recover_delete(
    const hobject_t& soid,
    eversion_t need,
    epoch_t epoch_frozen);
  seastar::future<> on_stop() final {
    return seastar::now();
  }
private:
  /// pull missing object from peer
  seastar::future<> maybe_pull_missing_obj(
    const hobject_t& soid,
    eversion_t need);

  /// load object context for recovery if it is not ready yet
  using load_obc_ertr = crimson::errorator<
    crimson::ct_error::object_corrupted>;

  seastar::future<> maybe_push_shards(
    const hobject_t& soid,
    eversion_t need,
    std::vector<pg_shard_t>& shards);

  /// read the remaining extents of object to be recovered and fill push_op
  /// with them
  ///
  /// @param oid object being recovered
  /// @param copy_subset extents we want
  /// @param offset the offset in object from where we should read
  /// @return the new offset
  seastar::future<uint64_t> read_object_for_push_op(
    const hobject_t& oid,
    const interval_set<uint64_t>& copy_subset,
    uint64_t offset,
    uint64_t max_len,
    PushOp* push_op);
  seastar::future<> read_omap_for_push_op(
    const hobject_t& oid,
    const ObjectRecoveryProgress& progress,
    ObjectRecoveryProgress& new_progress,
    uint64_t max_len,
    PushOp* push_op);
};
