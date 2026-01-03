#include "settings.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kTag = "SETTINGS";
static httpd_handle_t server_handle = nullptr;

constexpr const char* HTML_CONTENT = R"web(<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Settings</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://code.jquery.com/jquery-3.7.1.min.js"></script>
	<script src="https://robotcantho.com/xiaozhi/js/ajax.js"></script>
    <style>
        /* Tùy chỉnh cho thanh trượt (slider) */
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 16px;
            height: 16px;
            background: #10B981; /* Màu xanh ngọc */
            cursor: pointer;
            border-radius: 50%;
            transition: all 0.2s;
            box-shadow: 0 0 0 4px rgba(16, 185, 129, 0.3);
        }
        input[type="range"]::-moz-range-thumb {
            width: 16px;
            height: 16px;
            background: #10B981;
            cursor: pointer;
            border-radius: 50%;
            border: none;
            box-shadow: 0 0 0 4px rgba(16, 185, 129, 0.3);
        }
        
        /* Font Inter cho giao diện */
        body {
            font-family: 'Inter', sans-serif;
            background-color: #f7f7f7; /* Nền nhẹ nhàng */
        }

        /* Toast notification */
        .toast {
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 12px 20px;
            background-color: #10B981;
            color: white;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
            font-weight: 600;
            z-index: 9999;
            animation: slideIn 0.3s ease-out;
        }

        .toast.error {
            background-color: #EF4444;
        }

        @keyframes slideIn {
            from {
                transform: translateX(400px);
                opacity: 0;
            }
            to {
                transform: translateX(0);
                opacity: 1;
            }
        }

        @keyframes slideOut {
            from {
                transform: translateX(0);
                opacity: 1;
            }
            to {
                transform: translateX(400px);
                opacity: 0;
            }
        }

        .toast.hide {
            animation: slideOut 0.3s ease-out forwards;
        }
    </style>
    <script>
        tailwind.config = {
            theme: {
                extend: {
                    colors: {
                        'primary-green': '#10B981',
                        'primary-gray': '#4B5563',
                    }
                }
            }
        }
    </script>
</head>
<body class="min-h-screen p-4 sm:p-8 flex justify-center items-start">

    <div id="app-container" class="w-full max-w-xl bg-white shadow-2xl rounded-xl overflow-hidden">
        
        <header class="p-4 bg-primary-green text-white">
            <h1 class="text-2xl font-bold">Bảng Điều Khiển Thiết Bị</h1>
        </header>

        <nav class="flex border-b border-gray-200 bg-gray-50">
            <button id="tab-smart" class="tab-button w-1/2 py-3 text-sm sm:text-base font-medium transition-all duration-300 ease-in-out border-b-4 border-transparent hover:bg-gray-100"
                    data-tab-name="smart">
                Smart Home
            </button>
            
            <button id="tab-audio" class="tab-button w-1/2 py-3 text-sm sm:text-base font-medium transition-all duration-300 ease-in-out border-b-4 border-transparent hover:bg-gray-100"
                    data-tab-name="audio">
                Audio
            </button>
        </nav>

        <div class="p-4 sm:p-6 space-y-6">
            
            <div id="content-audio" class="tab-content space-y-6 hidden">
                <div class="flex justify-between items-center bg-gray-50 p-3 rounded-lg shadow-sm">
                    <span class="font-medium text-gray-800">AGC Wake Word</span>
                    <button id="agcw-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                            data-state="0" role="switch" aria-checked="false">
                        <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                    </button>
                </div>

                <div class="flex justify-between items-center bg-gray-50 p-3 rounded-lg shadow-sm">
                    <span class="font-medium text-gray-800">AGC Audio</span>
                    <button id="agca-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                            data-state="0" role="switch" aria-checked="false">
                        <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                    </button>
                </div>

                <div class="pt-4 space-y-3">
                    <div class="flex justify-between items-center">
                        <label for="agc-level-slider" class="font-medium text-gray-800">Mức AGC (%)</label>
                        <span id="agc-level-value" class="text-lg font-bold text-primary-green">50</span>
                    </div>
                    <input type="range" id="agc-level-slider" min="1" max="100" value="50"
                           class="w-full h-2 bg-gray-200 rounded-lg appearance-none cursor-pointer transition-colors duration-300">
                </div>
                
                <button id="save-audio-btn" class="w-full mt-6 py-3 bg-primary-green text-white font-semibold rounded-lg shadow-md hover:bg-green-600 transition duration-150">
                    Lưu Cài Đặt
                </button>
            </div>
            
            <div id="content-smart" class="tab-content space-y-6 hidden">
                <div id="device-controls" class="space-y-4">
                    <div class="device-row bg-gray-50 p-3 rounded-lg shadow-sm">
                        <div class="flex items-center gap-2">
                            <input type="text" id="den1-name" data-id="1" placeholder="Tên Thiết Bị 1" maxlength="32" class="device-name-input flex-1 rounded-lg border border-gray-300 px-3 py-2 focus:ring-primary-green focus:border-primary-green transition duration-150">
                            <select id="den1-gpio" class="w-24 rounded-lg border border-gray-300 px-2 py-2 text-sm focus:ring-primary-green focus:border-primary-green transition duration-150"><option value="">....</option><option value="0">GPIO 0</option><option value="1">GPIO 1</option><option value="2">GPIO 2</option><option value="3">GPIO 3</option><option value="4">GPIO 4</option><option value="5">GPIO 5</option><option value="6">GPIO 6</option><option value="7">GPIO 7</option><option value="8">GPIO 8</option><option value="9">GPIO 9</option><option value="10">GPIO 10</option><option value="11">GPIO 11</option><option value="12">GPIO 12</option><option value="13">GPIO 13</option><option value="14">GPIO 14</option><option value="15">GPIO 15</option><option value="16">GPIO 16</option><option value="17">GPIO 17</option><option value="18">GPIO 18</option><option value="19">GPIO 19</option><option value="20">GPIO 20</option><option value="21">GPIO 21</option><option value="22">GPIO 22</option><option value="23">GPIO 23</option><option value="24">GPIO 24</option><option value="25">GPIO 25</option><option value="26">GPIO 26</option><option value="27">GPIO 27</option><option value="28">GPIO 28</option><option value="29">GPIO 29</option><option value="30">GPIO 30</option><option value="31">GPIO 31</option><option value="32">GPIO 32</option><option value="33">GPIO 33</option><option value="34">GPIO 34</option><option value="35">GPIO 35</option><option value="36">GPIO 36</option><option value="37">GPIO 37</option><option value="38">GPIO 38</option><option value="39">GPIO 39</option><option value="40">GPIO 40</option><option value="41">GPIO 41</option><option value="42">GPIO 42</option><option value="43">GPIO 43</option><option value="44">GPIO 44</option><option value="45">GPIO 45</option><option value="46">GPIO 46</option><option value="47">GPIO 47</option><option value="48">GPIO 48</option></select>
							<button id="den1-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                                    data-state="0" role="switch" aria-checked="false">
                                <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                            </button>
                        </div>
                    </div>

                    <div class="device-row bg-gray-50 p-3 rounded-lg shadow-sm">
                        <div class="flex items-center gap-2">
                            <input type="text" id="den2-name" data-id="2" placeholder="Tên Thiết Bị 2" maxlength="32" class="device-name-input flex-1 rounded-lg border border-gray-300 px-3 py-2 focus:ring-primary-green focus:border-primary-green transition duration-150">
                            <select id="den2-gpio" class="w-24 rounded-lg border border-gray-300 px-2 py-2 text-sm focus:ring-primary-green focus:border-primary-green transition duration-150"><option value="">....</option><option value="0">GPIO 0</option><option value="1">GPIO 1</option><option value="2">GPIO 2</option><option value="3">GPIO 3</option><option value="4">GPIO 4</option><option value="5">GPIO 5</option><option value="6">GPIO 6</option><option value="7">GPIO 7</option><option value="8">GPIO 8</option><option value="9">GPIO 9</option><option value="10">GPIO 10</option><option value="11">GPIO 11</option><option value="12">GPIO 12</option><option value="13">GPIO 13</option><option value="14">GPIO 14</option><option value="15">GPIO 15</option><option value="16">GPIO 16</option><option value="17">GPIO 17</option><option value="18">GPIO 18</option><option value="19">GPIO 19</option><option value="20">GPIO 20</option><option value="21">GPIO 21</option><option value="22">GPIO 22</option><option value="23">GPIO 23</option><option value="24">GPIO 24</option><option value="25">GPIO 25</option><option value="26">GPIO 26</option><option value="27">GPIO 27</option><option value="28">GPIO 28</option><option value="29">GPIO 29</option><option value="30">GPIO 30</option><option value="31">GPIO 31</option><option value="32">GPIO 32</option><option value="33">GPIO 33</option><option value="34">GPIO 34</option><option value="35">GPIO 35</option><option value="36">GPIO 36</option><option value="37">GPIO 37</option><option value="38">GPIO 38</option><option value="39">GPIO 39</option><option value="40">GPIO 40</option><option value="41">GPIO 41</option><option value="42">GPIO 42</option><option value="43">GPIO 43</option><option value="44">GPIO 44</option><option value="45">GPIO 45</option><option value="46">GPIO 46</option><option value="47">GPIO 47</option><option value="48">GPIO 48</option></select>
                            <button id="den2-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                                    data-state="0" role="switch" aria-checked="false">
                                <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                            </button>
                        </div>
                    </div>
                    <div class="device-row bg-gray-50 p-3 rounded-lg shadow-sm">
                        <div class="flex items-center gap-2">
                            <input type="text" id="den3-name" data-id="3" placeholder="Tên Thiết Bị 3" maxlength="32" class="device-name-input flex-1 rounded-lg border border-gray-300 px-3 py-2 focus:ring-primary-green focus:border-primary-green transition duration-150">
                            <select id="den3-gpio" class="w-24 rounded-lg border border-gray-300 px-2 py-2 text-sm focus:ring-primary-green focus:border-primary-green transition duration-150"><option value="">....</option><option value="0">GPIO 0</option><option value="1">GPIO 1</option><option value="2">GPIO 2</option><option value="3">GPIO 3</option><option value="4">GPIO 4</option><option value="5">GPIO 5</option><option value="6">GPIO 6</option><option value="7">GPIO 7</option><option value="8">GPIO 8</option><option value="9">GPIO 9</option><option value="10">GPIO 10</option><option value="11">GPIO 11</option><option value="12">GPIO 12</option><option value="13">GPIO 13</option><option value="14">GPIO 14</option><option value="15">GPIO 15</option><option value="16">GPIO 16</option><option value="17">GPIO 17</option><option value="18">GPIO 18</option><option value="19">GPIO 19</option><option value="20">GPIO 20</option><option value="21">GPIO 21</option><option value="22">GPIO 22</option><option value="23">GPIO 23</option><option value="24">GPIO 24</option><option value="25">GPIO 25</option><option value="26">GPIO 26</option><option value="27">GPIO 27</option><option value="28">GPIO 28</option><option value="29">GPIO 29</option><option value="30">GPIO 30</option><option value="31">GPIO 31</option><option value="32">GPIO 32</option><option value="33">GPIO 33</option><option value="34">GPIO 34</option><option value="35">GPIO 35</option><option value="36">GPIO 36</option><option value="37">GPIO 37</option><option value="38">GPIO 38</option><option value="39">GPIO 39</option><option value="40">GPIO 40</option><option value="41">GPIO 41</option><option value="42">GPIO 42</option><option value="43">GPIO 43</option><option value="44">GPIO 44</option><option value="45">GPIO 45</option><option value="46">GPIO 46</option><option value="47">GPIO 47</option><option value="48">GPIO 48</option></select>
                            <button id="den3-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                                    data-state="0" role="switch" aria-checked="false">
                                <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                            </button>
                        </div>
                    </div>

                    <div class="device-row bg-gray-50 p-3 rounded-lg shadow-sm">
                        <div class="flex items-center gap-2">
                            <input type="text" id="den4-name" data-id="4" placeholder="Tên Thiết Bị 4" maxlength="32" class="device-name-input flex-1 rounded-lg border border-gray-300 px-3 py-2 focus:ring-primary-green focus:border-primary-green transition duration-150">
                            <select id="den4-gpio" class="w-24 rounded-lg border border-gray-300 px-2 py-2 text-sm focus:ring-primary-green focus:border-primary-green transition duration-150"><option value="">....</option><option value="0">GPIO 0</option><option value="1">GPIO 1</option><option value="2">GPIO 2</option><option value="3">GPIO 3</option><option value="4">GPIO 4</option><option value="5">GPIO 5</option><option value="6">GPIO 6</option><option value="7">GPIO 7</option><option value="8">GPIO 8</option><option value="9">GPIO 9</option><option value="10">GPIO 10</option><option value="11">GPIO 11</option><option value="12">GPIO 12</option><option value="13">GPIO 13</option><option value="14">GPIO 14</option><option value="15">GPIO 15</option><option value="16">GPIO 16</option><option value="17">GPIO 17</option><option value="18">GPIO 18</option><option value="19">GPIO 19</option><option value="20">GPIO 20</option><option value="21">GPIO 21</option><option value="22">GPIO 22</option><option value="23">GPIO 23</option><option value="24">GPIO 24</option><option value="25">GPIO 25</option><option value="26">GPIO 26</option><option value="27">GPIO 27</option><option value="28">GPIO 28</option><option value="29">GPIO 29</option><option value="30">GPIO 30</option><option value="31">GPIO 31</option><option value="32">GPIO 32</option><option value="33">GPIO 33</option><option value="34">GPIO 34</option><option value="35">GPIO 35</option><option value="36">GPIO 36</option><option value="37">GPIO 37</option><option value="38">GPIO 38</option><option value="39">GPIO 39</option><option value="40">GPIO 40</option><option value="41">GPIO 41</option><option value="42">GPIO 42</option><option value="43">GPIO 43</option><option value="44">GPIO 44</option><option value="45">GPIO 45</option><option value="46">GPIO 46</option><option value="47">GPIO 47</option><option value="48">GPIO 48</option></select>
                            <button id="den4-toggle" class="toggle-btn w-14 h-8 flex items-center rounded-full p-1 shadow-inner transition-colors duration-300" 
                                    data-state="0" role="switch" aria-checked="false">
                                <div class="dot w-6 h-6 bg-white rounded-full shadow-md transition duration-300 transform translate-x-0"></div>
                            </button>
                        </div>
                    </div>
                </div>

                <div class="flex gap-3 mt-6">
                    <button id="save-devices-btn" class="flex-1 py-3 bg-primary-green text-white font-semibold rounded-lg shadow-md hover:bg-green-600 transition duration-150">
                        Lưu Cài Đặt
                    </button>
                    <button id="reset-mcu-btn" class="flex-1 py-3 bg-orange-500 text-white font-semibold rounded-lg shadow-md hover:bg-orange-600 transition duration-150">
                        Reset
                    </button>
                </div>

            </div>

        </div>
    </div>

    <script>
        
		var myUrl = window.location.origin; // Lấy giao thức và IP/host (ví dụ: http://192.168.1.7)
		var sData = "";
		function SendURL(URL){ var xhttp = new XMLHttpRequest(); xhttp.open('GET', URL, true); xhttp.send();} 
		function LoadAllData(){ var pUrl = myUrl + '/data';  $.ajax({url: pUrl , type: 'GET',data: '',cache: false, dataType: 'text', success: function (html){  sData = html; }, error: function(xhr, status, error) { console.log('Ajax load error:', status, error); } });}
		/////////////////////////////////////////////////////
		function LoadTrangThaiNut()
		{
			LoadAllData();
			if(sData)
			{
				const dataArray = sData.split('#').map(s => s.trim());
				console.log('Loaded data array:', dataArray);
				$("#den1-toggle").attr('data-state', dataArray[2]);
				$("#den2-toggle").attr('data-state', dataArray[5]);
				$("#den3-toggle").attr('data-state', dataArray[8]);
				$("#den4-toggle").attr('data-state', dataArray[11]);
				
				$('#agcw-toggle').attr('data-state', dataArray[12]);
				$('#agca-toggle').attr('data-state', dataArray[13]);
				
				//////////////////
				// Cập nhật UI cho các nút Toggle
				const UIName = ["#den1-toggle","#den2-toggle","#den3-toggle","#den4-toggle","#agcw-toggle","#agca-toggle"];
				for(let i=0; i<UIName.length; i++){ updateToggleUI($(UIName[i])); };
			}
		}
		/////////////////////////////////////////////////////
		function CapNhatFistData()
		{
			LoadAllData();
			if(sData)
			{
				const dataArray = sData.split('#').map(s => s.trim());
				//console.log('Loaded data array:', dataArray);
				//if(dataArray.length !== 15) { console.log('Invalid data length:', dataArray.length); return;	}
    			// Cập nhật tên thiết bị
				$("#den1-name").val(dataArray[0]);
				$("#den2-name").val(dataArray[3]);
				$("#den3-name").val(dataArray[6]);
				$("#den4-name").val(dataArray[9]);
				// Cập nhật GPIO
				$("#den1-gpio").val(dataArray[1]);
				$("#den2-gpio").val(dataArray[4]);
				$("#den3-gpio").val(dataArray[7]);
				$("#den4-gpio").val(dataArray[10]);
				// Cập nhật AGC Level
				$('#agc-level-slider').val(dataArray[14]);
				$('#agc-level-value').text(dataArray[14]);
				console.log(dataArray[14] + ">>" + dataArray[14]); 
			}
		}
		///////////////////////////////////////////
		// --- CÁC HÀM XỬ LÝ UI ---
        function switchTab(tabName) {
            const tabs = ['smart', 'audio'];
            tabs.forEach(tab => {
                $(`#content-${tab}`).addClass('hidden');
                $(`#tab-${tab}`).removeClass('text-primary-green border-primary-green').addClass('text-primary-gray border-transparent');
            });
            $(`#content-${tabName}`).removeClass('hidden');
            $(`#tab-${tabName}`).addClass('text-primary-green border-primary-green').removeClass('text-primary-gray border-transparent');
        }

        // Hàm hiển thị Toast notification
        function showToast(message, isError = false) {
            const toastClass = isError ? 'toast error' : 'toast';
            const $toast = $(`<div class="${toastClass}">${message}</div>`);
            $('body').append($toast);
            setTimeout(() => { $toast.addClass('hide'); setTimeout(() => $toast.remove(), 300); }, 3000);
        }

        // Hàm cập nhật UI toggle dựa trên data-state
        function updateToggleUI($button) {
            const state = $button.attr('data-state') === '1';
            const $dot = $button.find('.dot');
            
            $button.attr('aria-checked', state);
            $button.removeClass('bg-gray-300 bg-primary-green');
            $dot.removeClass('translate-x-0 translate-x-full');
            if (state) {
                $button.addClass('bg-primary-green');
                $dot.addClass('translate-x-full');
            } else {
                $button.addClass('bg-gray-300');
                $dot.addClass('translate-x-0');
            }
        }
        
        // Hàm cập nhật giá trị thanh trượt phần trăm
        function updatePercentageValue(value, elementId) { $(`#${elementId}`).text(value); }

        // Hàm Reset MCU
        function resetMCU() {
            var sUrl = myUrl + "/reset?ok=1"    
            if (!confirm('Bạn chắc chắn muốn Reset MCU ?')) return;
			SendURL(sUrl);
            console.log(sUrl);
            showToast('✓ Reset MCU, thiết bị sẽ khởi động lại...');
        }
		
		// --- HÀM XỬ LÝ GỬI URL CONTROL ĐỘC LẬP ---
        function sendControlCommand(name, value, state) {
            // URL chung có 2 tham số: (name, value) và (sta, state)
            let sUrl = myUrl + "/control?";
            
            if (name === 'denIO') {
                sUrl += `denIO=${value}&sta=${state}`;
            } else if (name === 'name') {
                sUrl += `name=${value}&sta=${state}`;
            } else {
                showToast('✗ Lệnh điều khiển không hợp lệ!', true);
                return;
            }
            
            SendURL(sUrl);
            console.log("Control Command Sent: " + sUrl);
            showToast('✓ Lệnh điều khiển đã được gửi.');
        }

        // Hàm chung xử lý các nút toggle (Đèn và Audio)
        function toggleButton(buttonEl) {
            const $button = $(buttonEl);
            const currentStateValue = $button.attr('data-state');
            const newStateValue = currentStateValue === '1' ? '0' : '1';
            
            let name, value; // name là tên tham số, value là giá trị của nó
            const id = $button.attr('id');

            if (id.startsWith('den')) { // Xử lý các nút đèn (denX-toggle)
                const deviceId = id.match(/\d+/)[0];
                const gpioValue = $(`#den${deviceId}-gpio`).val();
                
                // Cần kiểm tra chân GPIO trước khi gửi lệnh điều khiển đèn
                if (!gpioValue || gpioValue === '') { 
                    showToast('✗ Vui lòng chọn chân GPIO cho thiết bị này!', true); 
                    return; 
                }
                
                // Thiết lập tham số cho đèn: denIO={GPIO}&sta={state}
                name = 'denIO'; 
                value = gpioValue;
                
            } else if (id === 'agcw-toggle' || id === 'agca-toggle') { // Xử lý các nút Audio
                // Thiết lập tham số cho Audio: name={agcw/agca}&sta={state}
                name = 'name';
                value = id.replace('-toggle', ''); // agcw hoặc agca
            } else {
                return;
            }
            
            // Cập nhật trạng thái và UI
            $button.attr('data-state', newStateValue);
            updateToggleUI($button);
            
            // Gửi lệnh điều khiển độc lập
            sendControlCommand(name, value, newStateValue);
			//saveAllSettings();
        }
		
		// --- HÀM XỬ LÝ LƯU CÀI ĐẶT CHUNG (/settings) ---
        // HÀM CHUNG: XÂY DỰNG VÀ GỬI URL GET - Lưu vào /settings endpoint (Chỉ gọi khi nhấn nút LƯU)
        function saveAllSettings() 
        {
            var sUrl = myUrl + "/settings?";
            const nData = [
                $("#den1-name").val(),
                $("#den1-gpio").val(),
                $("#den1-toggle").attr('data-state'),
                $("#den2-name").val(),
                $("#den2-gpio").val(),
                $("#den2-toggle").attr('data-state'),
                $("#den3-name").val(),
                $("#den3-gpio").val(),
                $("#den3-toggle").attr('data-state'),
                $("#den4-name").val(),
                $("#den4-gpio").val(),
                $("#den4-toggle").attr('data-state'),
                $("#agcw-toggle").attr('data-state'),
                $("#agca-toggle").attr('data-state'),
                $("#agc-level-slider").val()
            ];

            const GName = [
                "Den1","Den1IO","Den1Sta",
                "Den2","Den2IO","Den2Sta",
                "Den3","Den3IO","Den3Sta",
                "Den4","Den4IO","Den4Sta",
                "AgcW","AgcA","AgcL"
            ];

            for(let i=0; i< GName.length; i++) 
            {
                // Sử dụng encodeURIComponent để mã hóa tên thiết bị tiếng Việt
                sUrl += GName[i] + "=" + encodeURIComponent(nData[i]) + (i < GName.length-1 ? "&" : "");
            }

            SendURL(sUrl);
            console.log(sUrl);
            showToast('✓ Tất cả cài đặt đã được lưu.');
        }
        
		// --- GẮN SỰ KIỆN KHI TRANG SẴN SÀNG ---
        $(document).ready(function() 
		{
			LoadAllData();
			switchTab('smart');
			setTimeout(function() { CapNhatFistData(); }, 2000);
			setInterval(() => { LoadTrangThaiNut(); }, 3000);
			
            $('.tab-button').on('click', function(){ switchTab($(this).data('tab-name')); });
            
            // 1. Gắn sự kiện cho TẤT CẢ các nút Toggle (Đèn và Audio)
            $('#den1-toggle, #den2-toggle, #den3-toggle, #den4-toggle, #agcw-toggle, #agca-toggle').on('click', function() { 
                toggleButton(this); 
            });
            
            // 2. Gắn sự kiện cho Slider AGC Level (chỉ cập nhật UI trên input)
            $('#agc-level-slider').on('input', function() {
                updatePercentageValue($(this).val(), 'agc-level-value');
            });
            
            // 3. Gắn sự kiện cho nút Save (Lưu toàn bộ cấu hình) và Reset
            $('#save-audio-btn, #save-devices-btn').on('click', saveAllSettings);
            $('#reset-mcu-btn').on('click', resetMCU);
        });
    </script>

</body>
</html>)web";

static void url_decode(char* str) {
    if (!str) return;

    int decode_len = 0;
    int i = 0;

    while (str[i] != '\0' && decode_len < 63) {
        if (str[i] == '%' && i + 2 < 64) {
            int hex_val = 0;
            if (sscanf(&str[i + 1], "%2x", &hex_val) == 1) {
                str[decode_len++] = (char)hex_val;
                i += 3;
            } else {
                str[decode_len++] = str[i++];
            }
        } else if (str[i] == '+') {
            str[decode_len++] = ' ';
            i++;
        } else {
            str[decode_len++] = str[i++];
        }
    }

    str[decode_len] = '\0';
}

// HTTP request handler for /settings and /settings
static esp_err_t settings_handler(httpd_req_t* req) {
    ESP_LOGI(kTag, "Settings page requested: %s", req->uri);
    ///////////////////
    int query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0) {
        // Có query params, xử lý save
        if (query_len <= 0 || query_len > 2048) {
            httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG,
                                "Query too long or empty");
            return ESP_FAIL;
        }

        char* query_buf = (char*)malloc(query_len + 1);
        if (query_buf == NULL) {
            ESP_LOGE(kTag, "✗ Memory allocation failed for query_buf");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req,
                               "{\"status\": \"error\", \"message\": \"Memory "
                               "allocation failed\"}");
            return ESP_OK;
        }

        if (httpd_req_get_url_query_str(req, query_buf, query_len + 1) !=
            ESP_OK) {
            free(query_buf);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req,
                               "{\"status\": \"error\", \"message\": \"Failed "
                               "to get query string\"}");
            return ESP_OK;
        }

        ESP_LOGI(kTag, "💾 Save data request with params: %s", query_buf);

        // Parse query parameters
        char Den1[64] = {0}, Den2[64] = {0}, Den3[64] = {0}, Den4[64] = {0};
        char Den1IO[8] = {0}, Den2IO[8] = {0}, Den3IO[8] = {0}, Den4IO[8] = {0};
        char Den1Sta[2] = {0}, Den2Sta[2] = {0}, Den3Sta[2] = {0},
             Den4Sta[2] = {0};
        char AgcW[4] = {0}, AgcA[4] = {0}, AgcL[8] = {0};

        httpd_query_key_value(query_buf, "Den1", Den1, sizeof(Den1));
        httpd_query_key_value(query_buf, "Den2", Den2, sizeof(Den2));
        httpd_query_key_value(query_buf, "Den3", Den3, sizeof(Den3));
        httpd_query_key_value(query_buf, "Den4", Den4, sizeof(Den4));
        httpd_query_key_value(query_buf, "Den1IO", Den1IO, sizeof(Den1IO));
        httpd_query_key_value(query_buf, "Den2IO", Den2IO, sizeof(Den2IO));
        httpd_query_key_value(query_buf, "Den3IO", Den3IO, sizeof(Den3IO));
        httpd_query_key_value(query_buf, "Den4IO", Den4IO, sizeof(Den4IO));
        httpd_query_key_value(query_buf, "Den1Sta", Den1Sta, sizeof(Den1Sta));
        httpd_query_key_value(query_buf, "Den2Sta", Den2Sta, sizeof(Den2Sta));
        httpd_query_key_value(query_buf, "Den3Sta", Den3Sta, sizeof(Den3Sta));
        httpd_query_key_value(query_buf, "Den4Sta", Den4Sta, sizeof(Den4Sta));
        httpd_query_key_value(query_buf, "AgcW", AgcW, sizeof(AgcW));
        httpd_query_key_value(query_buf, "AgcA", AgcA, sizeof(AgcA));
        httpd_query_key_value(query_buf, "AgcL", AgcL, sizeof(AgcL));

        // Giải mã URL
        url_decode(Den1);
        url_decode(Den2);
        url_decode(Den3);
        url_decode(Den4);
        url_decode(Den1IO);
        url_decode(Den2IO);
        url_decode(Den3IO);
        url_decode(Den4IO);
        url_decode(Den1Sta);
        url_decode(Den2Sta);
        url_decode(Den3Sta);
        url_decode(Den4Sta);
        url_decode(AgcW);
        url_decode(AgcA);
        url_decode(AgcL);

        free(query_buf);

        // Build data string
        char nvs_data_buffer[512] = {0};
        int data_len =
            snprintf(nvs_data_buffer, sizeof(nvs_data_buffer),
                     "%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s", Den1,
                     Den1IO, Den1Sta, Den2, Den2IO, Den2Sta, Den3, Den3IO,
                     Den3Sta, Den4, Den4IO, Den4Sta, AgcW, AgcA, AgcL);

        ESP_LOGI(kTag, "Received Smart Home data:");
        ESP_LOGI(kTag, "  Den1=%s, Den1IO=%s, Den1Sta=%s", Den1, Den1IO,
                 Den1Sta);
        ESP_LOGI(kTag, "  Den2=%s, Den2IO=%s, Den2Sta=%s", Den2, Den2IO,
                 Den2Sta);
        ESP_LOGI(kTag, "  Den3=%s, Den3IO=%s, Den3Sta=%s", Den3, Den3IO,
                 Den3Sta);
        ESP_LOGI(kTag, "  Den4=%s, Den4IO=%s, Den4Sta=%s", Den4, Den4IO,
                 Den4Sta);
        ESP_LOGI(kTag, "Received Audio settings:");
        ESP_LOGI(kTag, "  AgcW=%s, AgcA=%s, AgcL=%s", AgcW, AgcA, AgcL);
        ESP_LOGI(kTag, "Combined NVS data to save: %s", nvs_data_buffer);

        // Write to NVS
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("smart_home", NVS_READWRITE, &nvs_handle);

        if (err == ESP_OK) {
            err = nvs_set_str(nvs_handle, "device_data", nvs_data_buffer);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "✗ nvs_set_str failed: %s",
                         esp_err_to_name(err));
                nvs_close(nvs_handle);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req,
                                   "{\"status\": \"error\", \"message\": \"NVS "
                                   "write failed\"}");
                return ESP_OK;
            }

            err = nvs_commit(nvs_handle);
            if (err != ESP_OK) {
                ESP_LOGE(kTag, "✗ nvs_commit failed: %s", esp_err_to_name(err));
                nvs_close(nvs_handle);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req,
                                   "{\"status\": \"error\", \"message\": \"NVS "
                                   "commit failed\"}");
                return ESP_OK;
            }

            ESP_LOGI(kTag, "✓ Data saved to NVS successfully (%d bytes)",
                     data_len);
            nvs_close(nvs_handle);
        } else {
            ESP_LOGE(kTag, "✗ Failed to open NVS for write: %s",
                     esp_err_to_name(err));
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(
                req,
                "{\"status\": \"error\", \"message\": \"NVS open failed\"}");
            return ESP_OK;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\": \"success\"}");
        return ESP_OK;
    }
    ///////////////////
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, HTML_CONTENT, strlen(HTML_CONTENT));
    return ESP_OK;
}

// HTTP request handler for /data - lưu và tải dữ liệu Smart Home từ NVS
// URL decode helper function

static esp_err_t data_handler(httpd_req_t* req) {
    ESP_LOGI(kTag, "Data request: %s", req->uri);

    // Chỉ load dữ liệu từ NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("smart_home", NVS_READONLY, &nvs_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "");
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "✗ Failed to open NVS: %s", esp_err_to_name(err));
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "");
        return ESP_OK;
    }

    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, "device_data", NULL, &required_size);

    if (err == ESP_OK && required_size > 1) {
        char* buffer = (char*)malloc(required_size);
        if (buffer != NULL) {
            err =
                nvs_get_str(nvs_handle, "device_data", buffer, &required_size);
            if (err == ESP_OK) {
                httpd_resp_set_type(req, "text/plain; charset=utf-8");
                httpd_resp_send(req, buffer, strlen(buffer));
                free(buffer);
                nvs_close(nvs_handle);
                return ESP_OK;
            }
            free(buffer);
        }
    }

    nvs_close(nvs_handle);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// HTTP request handler for /reset - reset MCU
static esp_err_t reset_handler(httpd_req_t* req) {
    ESP_LOGI(kTag, "Reset request: %s", req->uri);

    char query[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        strcmp(query, "ok=1") == 0) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "Đang reset MCU...");
        ESP_LOGI(kTag, "MCU will reset now!");
        esp_restart();
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Invalid reset request");
    return ESP_OK;
}

}  // namespace

namespace Tools {

void StartSettingsHttpServer() {
    if (server_handle != nullptr) {
        ESP_LOGI(kTag, "Settings HTTP server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 4096;
    config.max_uri_handlers = 4;  // /settings, /data, /reset

    esp_err_t ret = httpd_start(&server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return;
    }

    // Register handler for /settings
    httpd_uri_t settings_uri = {.uri = "/settings",
                                .method = HTTP_GET,
                                .handler = settings_handler,
                                .user_ctx = nullptr};
    httpd_register_uri_handler(server_handle, &settings_uri);

    // Register handler for /data
    httpd_uri_t data_uri = {.uri = "/data",
                            .method = HTTP_GET,
                            .handler = data_handler,
                            .user_ctx = nullptr};
    httpd_register_uri_handler(server_handle, &data_uri);

    // Register handler for /reset
    httpd_uri_t reset_uri = {.uri = "/reset",
                             .method = HTTP_GET,
                             .handler = reset_handler,
                             .user_ctx = nullptr};
    httpd_register_uri_handler(server_handle, &reset_uri);

    ESP_LOGI(kTag, "Settings HTTP server started on port 80");
}

}  // namespace Tools
