////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include "async_query.hpp"

#include "realm_coordinator.hpp"

using namespace realm;
using namespace realm::_impl;

AsyncQuery::AsyncQuery(SortOrder sort,
                       std::unique_ptr<SharedGroup::Handover<Query>> handover,
                       std::unique_ptr<AsyncQueryCallback> callback,
                       RealmCoordinator& parent)
: m_sort(std::move(sort))
, m_query_handover(std::move(handover))
, m_callback(std::move(callback))
, parent(parent.shared_from_this())
{
}

void AsyncQuery::get_results(const SharedRealm& realm, SharedGroup& sg, std::vector<std::function<void()>>& ret)
{
    if (!m_callback->is_for_current_thread()) {
        return;
    }

    if (m_error) {
        ret.emplace_back([self = shared_from_this()] {
            self->m_callback->error(self->m_error);
            RealmCoordinator::unregister_query(*self);
        });
        return;
    }

    if (!m_tv_handover) {
        return;
    }
    if (m_tv_handover->version < sg.get_version_of_current_transaction()) {
        // async results are stale; ignore
        return;
    }
    auto r = Results(realm,
                     m_sort,
                     std::move(*sg.import_from_handover(std::move(m_tv_handover))));
    auto version = sg.get_version_of_current_transaction();
    ret.emplace_back([r = std::move(r), version, &sg, self = shared_from_this()] {
        if (sg.get_version_of_current_transaction() == version) {
            self->m_callback->deliver(std::move(r));
        }
    });
}

void AsyncQuery::prepare_update()
{
    // This function must not touch m_tv_handover as it is called without the
    // relevant lock held (so that another thread can consume m_tv_handover
    // while this is running)

    REALM_ASSERT(m_sg);

    if (m_tv.is_attached()) {
        m_did_update = m_tv.sync_if_needed();
    }
    else {
        m_tv = m_query->find_all();
        if (m_sort) {
            m_tv.sort(m_sort.columnIndices, m_sort.ascending);
        }
        m_did_update = true;
    }
}

void AsyncQuery::prepare_handover()
{
    // Even if the TV didn't change, we need to re-export it if the previous
    // export has not been consumed yet, as the old handover object is no longer
    // usable due to the version not matching
    if (m_did_update || (m_tv_handover && m_tv_handover->version != m_sg->get_version_of_current_transaction())) {
        m_tv_handover = m_sg->export_for_handover(m_tv, ConstSourcePayload::Copy);
    }

    if (m_did_update) {
        m_callback->update_ready();
    }
}

void AsyncQuery::set_error(std::exception_ptr err)
{
    if (!m_error) {
        m_error = err;
        m_callback->update_ready();
    }
}

SharedGroup::VersionID AsyncQuery::version() const noexcept
{
    if (m_tv_handover)
        return m_tv_handover->version;
    if (m_query_handover)
        return m_query_handover->version;
    return SharedGroup::VersionID{};
}

void AsyncQuery::attach_to(realm::SharedGroup& sg)
{
    REALM_ASSERT(!m_sg);

    m_query = sg.import_from_handover(std::move(m_query_handover));
    m_sg = &sg;
}

void AsyncQuery::detatch()
{
    REALM_ASSERT(m_sg);

    m_query_handover = m_sg->export_for_handover(*m_query, MutableSourcePayload::Move);
    m_sg = nullptr;
}
