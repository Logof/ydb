#include "util.h"

#include <util/stream/file.h>
#include <util/string/strip.h>

namespace NYq {

TRetryLimiter::TRetryLimiter(ui64 retryCount, const TInstant& retryCounterUpdatedAt, double retryRate)
    : RetryCount(retryCount), RetryCounterUpdatedAt(retryCounterUpdatedAt), RetryRate(retryRate) {
}

void TRetryLimiter::Assign(ui64 retryCount, const TInstant& retryCounterUpdatedAt, double retryRate) {
    RetryCount = retryCount;
    RetryCounterUpdatedAt = retryCounterUpdatedAt;
    RetryRate = retryRate;
}

bool TRetryLimiter::UpdateOnRetry(const TInstant& lastSeenAt, const TRetryPolicyItem& policy, const TInstant now) {
    auto lastPeriod = lastSeenAt - RetryCounterUpdatedAt;
    if (lastPeriod >= policy.RetryPeriod) {
        RetryRate = 0.0;
    } else {
        RetryRate += 1.0;
        auto rate = lastPeriod / policy.RetryPeriod * policy.RetryCount;
        if (RetryRate > rate) {
            RetryRate -= rate;
        } else {
            RetryRate = 0.0;
        }
    }
    bool shouldRetry = RetryRate < policy.RetryCount;
    if (shouldRetry) {
        RetryCount++;
        RetryCounterUpdatedAt = now;
    }
    return shouldRetry;
}

bool IsTerminalStatus(YandexQuery::QueryMeta::ComputeStatus status)
{
    return IsIn({ YandexQuery::QueryMeta::ABORTED_BY_USER, YandexQuery::QueryMeta::ABORTED_BY_SYSTEM,
        YandexQuery::QueryMeta::COMPLETED, YandexQuery::QueryMeta::FAILED }, status);
}

TDuration GetDuration(const TString& value, const TDuration& defaultValue)
{
    TDuration result = defaultValue;
    TDuration::TryParse(value, result);
    return result;
}

NConfig::TControlPlaneStorageConfig FillDefaultParameters(NConfig::TControlPlaneStorageConfig config)
{
    if (!config.GetIdempotencyKeysTtl()) {
        config.SetIdempotencyKeysTtl("10m");
    }

    if (!config.GetMaxRequestSize()) {
        config.SetMaxRequestSize(7 * 1024 * 1024);
    }

    if (!config.GetMaxCountConnections()) {
        config.SetMaxCountConnections(1000000);
    }

    if (!config.GetMaxCountQueries()) {
        config.SetMaxCountQueries(1000000);
    }

    if (!config.GetMaxCountBindings()) {
        config.SetMaxCountBindings(1000000);
    }

    if (!config.GetMaxCountJobs()) {
        config.SetMaxCountJobs(20);
    }

    if (!config.GetTasksBatchSize()) {
        config.SetTasksBatchSize(100);
    }

    if (!config.GetNumTasksProportion()) {
        config.SetNumTasksProportion(4);
    }

    if (!config.GetNumTasksProportion()) {
        config.SetNumTasksProportion(4);
    }

    if (!config.GetAutomaticQueriesTtl()) {
        config.SetAutomaticQueriesTtl("1d");
    }

    if (!config.GetTaskLeaseTtl()) {
        config.SetTaskLeaseTtl("30s");
    }

    if (!config.GetMetricsTtl()) {
        config.SetMetricsTtl("1d");
    }

    if (!config.HasTaskLeaseRetryPolicy()) {
        auto& taskLeaseRetryPolicy = *config.MutableTaskLeaseRetryPolicy();
        taskLeaseRetryPolicy.SetRetryCount(20);
        taskLeaseRetryPolicy.SetRetryPeriod("1d");
    }

    if (!config.RetryPolicyMappingSize()) {
        {
            auto& policyMapping = *config.AddRetryPolicyMapping();
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::UNAVAILABLE);
            auto& policy = *policyMapping.MutablePolicy();
            policy.SetRetryCount(20);
            policy.SetRetryPeriod("1d");
            // backoff is Zero()
        }
        {
            auto& policyMapping = *config.AddRetryPolicyMapping();
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::OVERLOADED);
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::EXTERNAL_ERROR);
            auto& policy = *policyMapping.MutablePolicy();
            policy.SetRetryCount(10);
            policy.SetRetryPeriod("1m");
            policy.SetBackoffPeriod("1s");
        }
        {
            auto& policyMapping = *config.AddRetryPolicyMapping();
            policyMapping.AddStatusCode(NYql::NDqProto::StatusIds::CLUSTER_OVERLOADED);
            auto& policy = *policyMapping.MutablePolicy();
            policy.SetRetryCount(3);
            policy.SetRetryPeriod("1d");
            policy.SetBackoffPeriod("2s");
        }
    }

    if (!config.GetStorage().GetToken() && config.GetStorage().GetOAuthFile()) {
        config.MutableStorage()->SetToken(StripString(TFileInput(config.GetStorage().GetOAuthFile()).ReadAll()));
    }

    if (!config.GetResultSetsTtl()) {
        config.SetResultSetsTtl("1d");
    }

    return config;
}

bool DoesPingTaskUpdateQueriesTable(const Fq::Private::PingTaskRequest& request) {
    return request.status() != YandexQuery::QueryMeta::COMPUTE_STATUS_UNSPECIFIED
        || !request.issues().empty()
        || !request.transient_issues().empty()
        || request.statistics()
        || !request.result_set_meta().empty()
        || request.ast()
        || request.ast_compressed().data()
        || request.plan()
        || request.plan_compressed().data()
        || request.has_started_at()
        || request.has_finished_at()
        || request.resign_query()
        || !request.created_topic_consumers().empty()
        || !request.dq_graph().empty()
        || !request.dq_graph_compressed().empty()
        || request.dq_graph_index()
        || request.state_load_mode()
        || request.has_disposition()
    ;
}

NYdb::TValue PackItemsToList(const TVector<NYdb::TValue>& items) {
    NYdb::TValueBuilder itemsAsList;
    itemsAsList.BeginList();
    for (const NYdb::TValue& item: items) {
        itemsAsList.AddListItem(item);
    }
    itemsAsList.EndList();
    return itemsAsList.Build();
}

std::pair<TString, TString> SplitId(const TString& id, char delim) {
    auto it = std::find(id.begin(), id.end(), delim);
    return std::make_pair(id.substr(0, it - id.begin()),
        (it != id.end() ? id.substr(it - id.begin() + 1) : TString{""}));
}

bool IsValidIntervalUnit(const TString& unit) {
    static constexpr std::array<std::string_view, 10> IntervalUnits = {
        "MICROSECONDS"sv,
        "MILLISECONDS"sv,
        "SECONDS"sv,
        "MINUTES"sv,
        "HOURS"sv,
        "DAYS"sv,
        "WEEKS"sv
    };
    return IsIn(IntervalUnits, unit);
}

} //namespace NYq
