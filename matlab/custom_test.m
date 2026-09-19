%pkg load tablicious;
pkg load io;

clear all; close all


function [yy] = smooth(y, span)
  yy = movmean(y, span);
end

function y = wrapToPi(x)
    % Wrap angles in radians to the interval [-pi, pi]
    y = angle(exp(1i * x));
end


%% Data reading
anchor_num = 4;

% location of the tag datafiles, there are two experiments in the sample_data folder
filename = 'location2_2';
path = './sample_data/';

%new_data = csv2cell('./sample_data/ground_truth.txt', ' ');

addpath("./utils");

%load all anchors into memory
% Anchors
for i = 1:anchor_num
    anc(i) = read_device_dw1000(path,filename,['anchor',num2str(i)],anchor_num,0);
end

%we'll just be looking at a single tag
% Tag
tag = read_device_dw1000(path,filename,['tag',num2str(1)],anchor_num,1);

% Anchor locations
[anc_loc,start_loc,end_loc] = read_gt(path,filename,anchor_num);

%% Parameter configuration

% Light speed
c = 299792458 %physconst('LightSpeed');

%supported frequencies by the DWM module, the DWM1000 supports these, but the DWM3000 does not.
%we'll have to recalculate for channels 5 and 9
% UWB Ch.1 (3.5 GHz)
fc1 = 3484.0e6;             % Center Frequency
lambda_ch1 = c/fc1*100;        % Wavelength

% UWB Ch.3 (4.5 GHz)
fc3 = 4484.8e6;             % Center Frequency
lambda_ch3 = c/fc3*100;        % Wavelength



% Time resolution of DW1000 (a very small number)
dw_time = 17.2/2^40;

% Time interval between two UWB packets
delta_anc = 615.6464e-6; % 615.65us

% Time interval between two rounds of localization
delta_turn = (delta_anc*anchor_num);
%% Separating UWB packets captured over different channels


% The DW1000 perform frequency hopping every 2 rounds of localization.
% This results in a repeating pattern where:
% - Rounds 1-2: UWB packets captured over Channel 1
% - Rounds 3-4: UWB packets captured over Channel 3
% The cycle repeats every 4 localization rounds.
idx_temp = mod(tag.idx,4);

% Initialize an empty array to store the useful indices
useful_idx = [];

% Filter out those indices where there exists packet loss
for  i = 1:length(idx_temp)-4

    % The pattern should be 0, 1, 2, 3, 0 which indicates no packet loss in the cycle
    if (idx_temp(i)==0) && (idx_temp(i+1)==1) && (idx_temp(i+2)==2) && (idx_temp(i+3)==3) && (idx_temp(i+4)==0)

        % If the pattern is correct, add the current index i to the useful_idx array
        useful_idx = [useful_idx,i];
    end
end


%indices of sampling done on channel 1 and 3
ch1_idx = sort([useful_idx,useful_idx+1]);
ch3_idx = sort([useful_idx+2,useful_idx+3]);








%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%my own testing: create a table with the POAs
anchor0_poa_ch1_anchor1 = anc(:,1).poa(:,ch1_idx)(2,:);
anchor0_poa_ch1_anchor1 = anchor0_poa_ch1_anchor1(1:2:end); %taking every other measurement
anchor1_poa_ch1_anchor0 = anc(:,2).poa(:,ch1_idx)(1,:);
anchor1_poa_ch1_anchor0 = anchor1_poa_ch1_anchor0(1:2:end);
phase_sums = wrapToPi(anchor1_poa_ch1_anchor0 + anchor0_poa_ch1_anchor1);


figure;
%plot(anchor0_poa_ch1_anchor1');hold on
%plot(anchor1_poa_ch1_anchor0');hold on
plot(phase_sums');hold on
title('phase angles of anchor 0 and 1 subtracted, ch1');
legend('AAAA','BBBB', 'CCCC');


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%












