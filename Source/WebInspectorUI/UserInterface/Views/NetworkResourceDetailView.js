/*
 * Copyright (C) 2017-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

WI.NetworkResourceDetailView = class NetworkResourceDetailView extends WI.NetworkDetailView
{
    constructor(resource, entry, delegate)
    {
        console.assert(resource instanceof WI.Resource, resource);

        super(resource, delegate);

        this._entry = entry;
        this._usesQuickieLayout = document.body.classList.contains("quickie-local-inspector");

        this.element.classList.add("resource");

        this._resourceContentView = null;
        this._headersContentView = null;
        this._formPayloadContentView = null;
        this._cookiesContentView = null;
        this._sizesContentView = null;
        this._timingContentView = null;
        this._securityContentView = null;
    }

    // Public

    get referencePage()
    {
        let currentContentView = this._contentBrowser.currentContentView;
        if (!currentContentView)
            return null;

        if (currentContentView === this._resourceContentView)
            return WI.ReferencePage.NetworkTab.PreviewPane;

        return currentContentView?.constructor.ReferencePage;
    }

    // Protected

    get preferredContentViewSetting()
    {
        if (!this._usesQuickieLayout)
            return super.preferredContentViewSetting;

        NetworkResourceDetailView._preferredContentViewSetting ??= new WI.Setting("quickie-network-detail-content-view-identifier", "headers");
        return NetworkResourceDetailView._preferredContentViewSetting;
    }

    attached()
    {
        super.attached();

        if (this._usesQuickieLayout) {
            for (let event of this._requestSummaryEvents)
                this.representedObject.addEventListener(event, this._requestSummaryDidChange, this);
            this._requestSummaryDidChange();
        }

        if (this._contentBrowser && this._contentViewCookie && "lineNumber" in this._contentViewCookie && "columnNumber" in this._contentViewCookie)
            this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("preview");
    }

    detached()
    {
        if (this._usesQuickieLayout) {
            for (let event of this._requestSummaryEvents)
                this.representedObject.removeEventListener(event, this._requestSummaryDidChange, this);
        }

        super.detached();
    }

    // ResourceHeadersContentView delegate

    headersContentViewGoToRequestData(headersContentView)
    {
        let payloadNavigationItem = this.detailNavigationItemForIdentifier("payload");
        if (payloadNavigationItem && !payloadNavigationItem.hidden) {
            this._contentBrowser.navigationBar.selectedNavigationItem = payloadNavigationItem;
            return;
        }

        this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("preview");

        this._resourceContentView.showRequest();
    }

    showRedirect(redirect, parentResource, parentEntry)
    {
        this._delegate?.showRedirect?.(redirect, parentResource, parentEntry || this._entry);
    }

    showRepresentedObject(representedObject)
    {
        this._delegate?.showRepresentedObject?.(representedObject);
    }

    // ResourceSizesContentView delegate

    sizesContentViewGoToHeaders(metricsContentView)
    {
        this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("headers");
    }

    sizesContentViewGoToRequestBody(metricsContentView)
    {
        this.headersContentViewGoToRequestData();
    }

    sizesContentViewGoToResponseBody(metricsContentView)
    {
        if (this._usesQuickieLayout) {
            this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("response");
            return;
        }

        this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("preview");

        this._resourceContentView.showResponse();
    }

    // Protected

    initialLayout()
    {
        if (this._usesQuickieLayout) {
            this._createRequestSummary();
            this.createDetailNavigationItem("headers", WI.UIString("Headers"));
            this.createDetailNavigationItem("preview", WI.UIString("Preview"));
            this.createDetailNavigationItem("payload", WI.unlocalizedString("Payload"));
            this.createDetailNavigationItem("response", WI.UIString("Response"));
            this.createDetailNavigationItem("timing", WI.UIString("Timing"));
            this.createDetailNavigationItem("cookies", WI.UIString("Cookies"));
            this.createDetailNavigationItem("sizes", WI.UIString("Sizes"));
            this._updatePayloadVisibility();
        } else {
            this.createDetailNavigationItem("preview", WI.UIString("Preview"));
            this.createDetailNavigationItem("headers", WI.UIString("Headers"));
            this.createDetailNavigationItem("cookies", WI.UIString("Cookies"));
            this.createDetailNavigationItem("sizes", WI.UIString("Sizes"));
            this.createDetailNavigationItem("timing", WI.UIString("Timing"));
        }
        this.createDetailNavigationItem("security", WI.UIString("Security"));

        super.initialLayout();

        if (this._usesQuickieLayout)
            this._contentBrowser.navigationBar.sizesToFit = true;
    }

    layout()
    {
        super.layout();

        if (this._requestSummaryNeedsRefresh)
            this._refreshRequestSummary();
    }

    // Private

    get _requestSummaryEvents()
    {
        return [
            WI.Resource.Event.URLDidChange,
            WI.Resource.Event.RequestHeadersDidChange,
            WI.Resource.Event.RequestDataDidChange,
            WI.Resource.Event.ResponseReceived,
            WI.Resource.Event.MetricsDidChange,
            WI.Resource.Event.SizeDidChange,
            WI.Resource.Event.LoadingDidFinish,
            WI.Resource.Event.LoadingDidFail,
        ];
    }

    _createRequestSummary()
    {
        let summary = this.element.appendChild(document.createElement("div"));
        summary.className = "request-summary";

        let identity = summary.appendChild(document.createElement("div"));
        identity.className = "request-summary-identity";
        this._requestSummaryMethodElement = identity.appendChild(document.createElement("span"));
        this._requestSummaryMethodElement.className = "request-summary-method";
        this._requestSummaryURLElement = identity.appendChild(document.createElement("bdi"));
        this._requestSummaryURLElement.className = "request-summary-url";

        let metrics = summary.appendChild(document.createElement("div"));
        metrics.className = "request-summary-metrics";
        this._requestSummaryStatusElement = metrics.appendChild(document.createElement("span"));
        this._requestSummaryStatusElement.className = "request-summary-status";
        this._requestSummarySizeElement = metrics.appendChild(document.createElement("span"));
        this._requestSummaryTimeElement = metrics.appendChild(document.createElement("span"));

        this._refreshRequestSummary();
    }

    _requestSummaryDidChange()
    {
        this._requestSummaryNeedsRefresh = true;
        this.needsLayout();
    }

    _refreshRequestSummary()
    {
        this._requestSummaryNeedsRefresh = false;

        let resource = this.representedObject;
        this._requestSummaryMethodElement.textContent = resource.requestMethod || "";
        this._requestSummaryMethodElement.hidden = !resource.requestMethod;
        this._requestSummaryURLElement.textContent = resource.displayURL;
        this._requestSummaryURLElement.title = resource.displayURL;

        let status = this._requestSummaryStatusElement;
        let code = resource.statusCode;
        status.classList.toggle("failed", !resource.canceled && (resource.failed || code >= 400));
        status.classList.toggle("success", !resource.failed && !resource.canceled && code >= 200 && code < 300);
        if (resource.canceled)
            status.textContent = WI.UIString("Canceled");
        else if (resource.failed)
            status.textContent = WI.unlocalizedString("Failed");
        else if (Number.isFinite(code) && code > 0)
            status.textContent = resource.statusText ? `${code} ${resource.statusText}` : code;
        else
            status.textContent = resource.finished ? "" : WI.unlocalizedString("Pending");
        status.hidden = !status.textContent;

        let size = resource.size;
        this._requestSummarySizeElement.hidden = !Number.isFinite(size) || size < 0;
        if (!this._requestSummarySizeElement.hidden) {
            let text = Number.bytesToString(size);
            this._requestSummarySizeElement.textContent = text;
            this._requestSummarySizeElement.setAttribute("aria-label", `${WI.UIString("Size")}: ${text}`);
        }

        let duration = resource.totalDuration;
        this._requestSummaryTimeElement.hidden = !Number.isFinite(duration) || duration < 0;
        if (!this._requestSummaryTimeElement.hidden) {
            let text = Number.secondsToString(duration);
            this._requestSummaryTimeElement.textContent = text;
            this._requestSummaryTimeElement.setAttribute("aria-label", `${WI.UIString("Duration")}: ${text}`);
        }

        this._updatePayloadVisibility();
    }

    _updatePayloadVisibility()
    {
        let item = this.detailNavigationItemForIdentifier("payload");
        if (!item)
            return;

        item.hidden = !this.representedObject.requestData;
        if (item.hidden && this._contentBrowser?.navigationBar.selectedNavigationItem === item)
            this._contentBrowser.navigationBar.selectedNavigationItem = this.detailNavigationItemForIdentifier("headers");
    }

    _showResourceContentView(cookie)
    {
        if (!this._resourceContentView)
            this._resourceContentView = this._contentBrowser.showContentViewForRepresentedObject(this.representedObject, false, {disableDropZone: true});

        this._contentBrowser.showContentView(this._resourceContentView, cookie);
    }

    showContentViewForIdentifier(identifier)
    {
        super.showContentViewForIdentifier(identifier);

        if (this._contentViewCookie && "lineNumber" in this._contentViewCookie && "columnNumber" in this._contentViewCookie)
            identifier = "preview";

        switch (identifier) {
        case "preview": {
            let cookie = this._contentViewCookie;
            if (this._usesQuickieLayout && !cookie) {
                cookie = {
                    [WI.ResourceClusterContentView.ContentViewIdentifierCookieKey]: WI.ResourceClusterContentView.Identifier.ResponseDOM,
                };
            }
            this._showResourceContentView(cookie);
            break;
        }
        case "payload":
            if (this.representedObject.hasRequestFormParameters()) {
                if (!this._formPayloadContentView)
                    this._formPayloadContentView = new WI.ResourceHeadersContentView(this.representedObject, this._entry, this, {payloadOnly: true});
                this._contentBrowser.showContentView(this._formPayloadContentView);
            } else {
                this._showResourceContentView(null);
                this._resourceContentView.showRequest();
            }
            break;
        case "response":
            this._showResourceContentView(null);
            this._resourceContentView.showResponse();
            break;
        case "headers":
            if (!this._headersContentView)
                this._headersContentView = new WI.ResourceHeadersContentView(this.representedObject, this._entry, this);
            this._contentBrowser.showContentView(this._headersContentView, this._contentViewCookie);
            break;
        case "cookies":
            if (!this._cookiesContentView)
                this._cookiesContentView = new WI.ResourceCookiesContentView(this.representedObject);
            this._contentBrowser.showContentView(this._cookiesContentView, this._contentViewCookie);
            break;
        case "sizes":
            if (!this._sizesContentView)
                this._sizesContentView = new WI.ResourceSizesContentView(this.representedObject, this);
            this._contentBrowser.showContentView(this._sizesContentView, this._contentViewCookie);
            break;
        case "timing":
            if (!this._timingContentView)
                this._timingContentView = new WI.ResourceTimingContentView(this.representedObject);
            this._contentBrowser.showContentView(this._timingContentView, this._contentViewCookie);
            break;
        case "security":
            if (!this._securityContentView)
                this._securityContentView = new WI.ResourceSecurityContentView(this.representedObject);
            this._contentBrowser.showContentView(this._securityContentView, this._contentViewCookie);
            break;
        }

        this._contentViewCookie = null;
    }
};
