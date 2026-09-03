%loads the CSV files from the test folder
%CSV files have 22 fields

function device = read_device_dw1000(path, file_name, device_name, anchor_num, device_type)
% read_device_dw1000  Parse UWB estimates from DW1000 log files
%
% ------ Input Parameters ------
% path       - Path to directory containing log files
% file_name  - Base name of the log file (without extensions)
% device_name - Name of UWB device (e.g., 'anc1', 'tag2')
% anchor_num - Total number of anchors in the system
% device_type- Device type: 1 for Tag, 0 for Anchor
%
% ------ Output Parameters ------
% device     - Structure containing parsed UWB data with fields:
%              name, rx_times, poa, abs, complex, phase_cali, rssi, cfo, idx

% Initialize device structure
device.name = device_name;
single_len = 0;  % Number of fields per anchor entry
format = '';      % Format string for text parsing

% Configure parameters based on device type
% Tag format: 7 hex fields + 1 float per anchor, plus index column
% Anchor format: 6 hex fields + 1 float per anchor, plus index column

%there are 3 of these per row for an anchor, 4 for a tag.
%I guess this is because each device communicates with 3 anchors / 4 anchors for a tag
if device_type == 1
	device_num = anchor_num;       % Tags communicate with all anchors
	single_len = 8;                % 8 fields per anchor entry
	hex_fields = 7;                % First 7 fields are hex values

	% Construct format string for tags:
	format = repmat('%s%s%s%s%s%s%s%f', 1, anchor_num);
	format = [format, '%s'];  % Add index column

elseif device_type == 0
	device_num = anchor_num - 1;   % Anchors communicate with other anchors
	single_len = 7;                % 7 fields per anchor entry
	hex_fields = 6;                % First 6 fields are hex values

	% Construct format string for anchors:
	format = repmat('%s%s%s%s%s%s%f', 1, anchor_num-1);
	format = [format, '%s'];  % Add index column
end

% Read data file with performance timing
tic
%data = table2cell(readtable(...
%	[path, file_name, '_', device_name, '.txt'],...
%	'Format', format));
filename = [path, file_name, '_', device_name, '.txt']
fid = fopen(filename, 'r');
data = textscan(fid, format, 'Delimiter', ',');
toc

% Convert hex strings to numeric values
for i = 1:size(data, 1)
	for j = 1:device_num
		% Process hex fields (convert to decimal)
		for k = 1:hex_fields
			col_idx = (j-1)*single_len + k;
			data{i, col_idx} = hex2dec(data{i, col_idx});
		end
	end
	% Convert index column from hex to decimal
	data{i, end} = hex2dec(data{i, end});
end

% Convert cell array to numeric matrix for efficient processing
data = cell2mat(data);

% Initialize data matrices
data_len = size(data, 1);
device.rx_times = zeros(anchor_num, data_len); %time of flight
device.poa = zeros(anchor_num, data_len); %CIR phase angle
device.abs = zeros(anchor_num, data_len); %CIR phase magnitude
device.complex = zeros(anchor_num, data_len); %complex number from the two above (actually, the two above were calculated with this)
device.phase_cali = zeros(anchor_num, data_len); %phase calibration offset
device.rssi = zeros(anchor_num, data_len); %signal strength
if device_type == 1
	device.cfo = zeros(anchor_num, data_len); %carrier frequency offset
end

device.idx = data(:, end);  % Store index values


%data format for an anchor, there are 3 per row, which are the other three anchors
%[CIR real (signed int)]    [CIR IM (signed int)]    [phase calib]    [rx preamble]    [gain]    [Time-of-flight]    [the ID of the anchor we were talking to]
%039d,                      0342,                    7f,              1f,              0019,     3534c61dc9,         1

%data format for a tag, there are 4 per row, which are all four other anchors
%[CIR real (signed int)]    [CIR IM (signed int)]    [phase calib]    [rx preamble]    [gain]    [Time-of-flight]    [carrier frequency offset]    [the ID of the anchor we were talking to]
%0389,                      f6f9,                    01,              3b,              01a1,     93412952a4,         00000064,                     0


% Process each anchor's data entries
for i = 1:device_num

	% Calculate column indices for current anchor
	base_col = (i-1)*single_len;

	%
	idx = data(1, base_col + single_len) + 1;  % Anchor ID from first row

	%recall this is Channel Impulse Response
	% Extract CIR components and convert to complex numbers
	real_part = data(:, base_col + 1)';
	imag_part = data(:, base_col + 2)';

	% Convert unsigned to signed integers (two's complement)
	real_part(real_part > 2^15) = real_part(real_part > 2^15) - 2^16;
	imag_part(imag_part > 2^15) = imag_part(imag_part > 2^15) - 2^16;

	% Store complex impulse response data
	device.complex(idx, :) = real_part + 1j*imag_part;
	device.abs(idx, :) = abs(device.complex(idx, :));
	device.poa(idx, :) = angle(device.complex(idx, :));

	% Extract and store phase calibration data
	device.phase_cali(idx, :) = data(:, base_col + 3)';

	% Calculate RSSI (detailed in DW1000 Manual)
	c = data(:, base_col + 5);  % max_gain_cir
	n = data(:, base_col + 4);  % rx_preamble_count
	a = 121.74;                 % Hardware-specific constant
	corrFac = 0.72;
	power = 10*log10(c.*(2^17)./(n.^2)) - a;
	power(power > -88) = power(power > -88) + (power(power > -88)+88)*corrFac;
	device.rssi(idx, :) = power;

	% Apply phase calibration offset
	device.poa(idx, :) = device.poa(idx, :) - ...
		device.phase_cali(idx, :)/128 * 2*pi;

	% Process device-specific timing data
	if device_type == 1  % Tag data processing
		% Store reception timestamps
		device.rx_times(idx, :) = data(:, base_col + 6)';

		% Calculate Carrier Frequency Offset (detailed in DW1000 Manual)
		temp = data(:, base_col + 7);
		temp(temp > 2^31) = temp(temp > 2^31) - 2^32;  % Signed conversion

		%some weird multiplication stuff
		device.cfo(idx, :) = -temp' * (998.4e6/2.0/1024.0/131072.0);

	else  % Anchor data processing
		% Store reception timestamps
		device.rx_times(idx, :) = data(:, base_col + 6)';
	end
end
end
