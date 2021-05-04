# BAVS 2.0

This filter is used by REXFlow to do traffic hijacking. It's super top secret. The filter works as follows:

## Happy Path, low detail

* Request comes in. If the rexflow headers are present (`x-flow-id`, `x-rexflow-wf-id`, `x-rexflow-task-id`), then the filter will read the request and immediately respond with HTTP 202 via the http utility `sendLocalReply()` function.

* If context parameters (closure transport) are enabled, then BAVS creates a request message for the upstream according to the variables provided by the context and its specification.

* The filter creates a `BAVSInboundRequest` object, and sends the request. This request reaches the original target: the upstream service.

* When the upstream service responds to the request sent from the previous step, the response is received by the callbacks on the `BavsInboundRequest`, which inherits from the `AsyncClient::Callbacks` abstract class. 

* If smart parameter matching is enabled, the `BavsInboundRequest` object parses the response from the upstream service, and inserts the relevant variables into the JSON received in the initial `decodeHeaders` and `decodeData` in the `BavsFilter` object.

* The `BavsInboundRequest` object creates a `BavsOutboundRequest` object with the appropriate data (see step above) and sends the request.

* The `BavsOutboundRequest` object implements callbacks that receive the response data. The object checks to make sure that the request succeeded.

## Unhappy Paths

The following are a mostly comprehensive list of departures from the above happy path.

### Failed to reach upstream service
If Envoy is unable to make a call to the uptream service (the `inbound` cluster), then we raise a `CONNECTION_ERROR`. To do this, we create a `BavsErrorRequest`. The contents of the request are the input headers, input data, `CONNECTION_ERROR` code, and a message stating that Envoy couldn't find the upstream service. The request is sent to Flowd, which does bookkeeping for the instance.

Note: This can happen in one of two ways: the call to `cluster_manager_.httpAsyncClientForCluster()` in `BavsFilter` can fail, OR we can immediately be sent to the `onFailure()` callback of the `BavsInboundRequest` object.

### Upstream Service Failed
We only check the http status header in closure transport. If the response is not a 200 of some flavor, then we raise a `TASK_ERROR`. We create a `BavsTaskErrorRequest` and send it. The `BavsTaskErrorRequest` request is a bit tricky...since some BPMN Service Tasks have a configurable retry, the `BavsTaskErrorRequest` may (depending on whether retries are present) retry the initial inbound request.

Once all retries (if any) are exhausted, the `BavsTaskErrorRequest` object raises a `TASK_ERROR` by sending an error request to Flowd. The contents are the input headers, input data, upstream service response headers, upstream service response data, and the appropriate error code + message.

### Failed to parse the context input
If the service io contract for the upstream (inbound) service states that a certain parameter of a certain type must be present, and Envoy does not find that parameter in the context input (this happens in `BavsFilter::decodeData()`), then we raise a `CONTEXT_INPUT_PARSING_ERROR`. This happens utilizes the `BavsErrorRequest` class, which just sends the error message. As usual, the error payload includes inbound headers+data, error code, and a message stating which parameter was expected but not found.

### Failed to parse the context output
This is similar to the case of failing to parse context input. If the service io contract states that the service will produce a variable with a certain name, and the service does not, then we raise a `CONTEXT_OUTPUT_PARSING_ERROR`. We include the input data+headers, output data+headers, error code, and a message describing which output parameter was not found. We use the `BavsErrorRequest` object.

### Failed Connecting to outbound service
If Envoy fails to make a request to the outbound edge (the sending of the request in the `InboundRequest::onSuccess()`. The request is defined in the `OutboundRequest` class.), then we raise a `CONNECTION_ERROR`. We use the `BavsErrorRequest` object. The data includes the headers+data that would've been sent to the outbound edge, error code, and error message.

## Bavs Json Parsing

Json parsing happens in `bavs_json.cc`.