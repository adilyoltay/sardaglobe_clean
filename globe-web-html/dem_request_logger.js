/**
 * DEM Request Logger - Captures all DEM/mesh requests for parity comparison
 */
(function() {
    'use strict';
    
    window.DemRequestLog = {
        requests: [],
        responses: [],
        
        clear: function() {
            this.requests = [];
            this.responses = [];
        },
        
        export: function() {
            return JSON.stringify({
                requests: this.requests,
                responses: this.responses.map(r => ({
                    url: r.url,
                    status: r.status,
                    dataLength: r.dataLength,
                    timestamp: r.timestamp
                }))
            }, null, 2);
        },
        
        printSummary: function() {
            console.log('=== DEM Request Summary ===');
            console.log('Total Requests:', this.requests.length);
            this.requests.forEach((req, i) => {
                console.log(`[${i}] ${req.url}`);
                console.log(`    Params: MESHN=${req.params.MESHN}, CN=${req.params.CN}`);
                if (req.params.cells && req.params.cells.length > 0) {
                    req.params.cells.forEach((cell, j) => {
                        console.log(`    Cell ${j+1}: z=${cell.z}, x=${cell.x}, y=${cell.y}`);
                        console.log(`             LLX=${cell.LLX}, LLY=${cell.LLY}, URX=${cell.URX}, URY=${cell.URY}`);
                    });
                }
            });
        }
    };
    
    // Parse DEM URL parameters
    function parseDemUrl(url) {
        const params = {cells: []};
        const urlObj = new URL(url);
        
        params.MESHN = urlObj.searchParams.get('MESHN');
        params.CN = parseInt(urlObj.searchParams.get('CN') || '1');
        params.FLOAT = urlObj.searchParams.get('FLOAT');
        
        for (let i = 1; i <= params.CN; i++) {
            const cell = {
                z: urlObj.searchParams.get(`C${i}z`),
                x: urlObj.searchParams.get(`C${i}x`),
                y: urlObj.searchParams.get(`C${i}y`),
                LLX: urlObj.searchParams.get(`C${i}LLX`),
                LLY: urlObj.searchParams.get(`C${i}LLY`),
                URX: urlObj.searchParams.get(`C${i}URX`),
                URY: urlObj.searchParams.get(`C${i}URY`)
            };
            params.cells.push(cell);
        }
        
        return params;
    }
    
    // Hook XMLHttpRequest
    const origXHROpen = XMLHttpRequest.prototype.open;
    const origXHRSend = XMLHttpRequest.prototype.send;
    
    XMLHttpRequest.prototype.open = function(method, url, ...args) {
        this._demUrl = url;
        this._demMethod = method;
        return origXHROpen.call(this, method, url, ...args);
    };
    
    XMLHttpRequest.prototype.send = function(body) {
        if (this._demUrl && (this._demUrl.includes('elevation_bbox') || this._demUrl.includes('yersun'))) {
            const url = this._demUrl;
            const params = parseDemUrl(url);
            
            window.DemRequestLog.requests.push({
                url: url,
                method: this._demMethod,
                params: params,
                timestamp: Date.now()
            });
            
            console.log('[DEM Request]', url);
            console.log('  MESHN:', params.MESHN, 'CN:', params.CN);
            
            // Capture response
            this.addEventListener('load', function() {
                window.DemRequestLog.responses.push({
                    url: url,
                    status: this.status,
                    dataLength: this.responseText ? this.responseText.length : 0,
                    timestamp: Date.now()
                });
            });
        }
        return origXHRSend.call(this, body);
    };
    
    // Hook fetch
    const origFetch = window.fetch;
    window.fetch = function(url, options) {
        if (typeof url === 'string' && (url.includes('elevation_bbox') || url.includes('yersun'))) {
            const params = parseDemUrl(url);
            
            window.DemRequestLog.requests.push({
                url: url,
                method: options?.method || 'GET',
                params: params,
                timestamp: Date.now()
            });
            
            console.log('[DEM Fetch]', url);
            console.log('  MESHN:', params.MESHN, 'CN:', params.CN);
        }
        return origFetch.call(this, url, options);
    };
    
    console.log('[DEM Logger] Initialized. Commands:');
    console.log('  DemRequestLog.requests     - All captured requests');
    console.log('  DemRequestLog.printSummary() - Print summary');
    console.log('  DemRequestLog.export()     - Export as JSON');
    console.log('  DemRequestLog.clear()      - Clear logs');
})();
