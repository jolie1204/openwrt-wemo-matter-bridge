# NetCam 센서 통합 개발 인계

이 문서는 개발 검토와 재현을 위한 기록입니다. 일반 사용자용 릴리스 안내나 설정 화면의 지원 기기 목록을 변경하지 않습니다.

## 2026-10-15 개발 재개 확인

최초 `netcam-sensor-integration` worktree는 이미 main으로 통합됐으며 현재
개발 checkout은 `openwrt-wemo-matter-bridge`입니다. 통합 구현은
`1560aac26c`, Matter stack 잠금 수정은 `0af53f2070`, SQLite reader 잠금
수정은 `df654a6c06`에 커밋되어 있습니다. 별도 runtime-lock worktree의
수정은 이전 작업 잔여본이며 최신 main보다 오래된 테스트/문서가 포함되어
있으므로 그대로 병합하지 않습니다.

현재 main에서 격리된 host regression suite 8개를 재실행해 모두 통과했습니다.
운영 브리지는 Core r23 / Matter r25로 실행 중이며 uptime은 약 5일 20시간,
controller snapshot의 14개 장치가 모두 online입니다. 관측한 Matter report의
StatusResponse는 SUCCESS였으나 Google Home 화면이나 실제 제어를 시험한
결과는 아닙니다.

NetCam DB 레코드는 retired 상태이며 재검색 후에도 active snapshot에
복귀하지 않았습니다. 카메라 UPnP HTTP 서비스의 기본 포트 49152에서 정확한
NetCamSensor description을 확인했고 브리지에서도 같은 description을
읽었습니다. host의 GetMotionStatus 응답은 idle / DetectionAvailable=1 /
BinaryState=0이었습니다. 영상 서비스의 source도 online을 반환했습니다.
host에서 NetCamSensor ST로 5초 SSDP 검색한 결과 응답은 0개였습니다.
이 단일 관측만으로 multicast 차단이나 camera daemon 결함을 확정하지 않습니다.

다음 조사 대상은 카메라 SSDP 광고와 M-SEARCH 응답, 브리지 수신 및 retired
레코드 재활성화 경로입니다. 실제 사람 움직임의 Occupancy 전환과 Google
Home 실시간 표시 검증도 남아 있습니다. 이번 확인에서 서비스를 재시작하거나
firmware, fabric, endpoint mapping을 변경하지 않았습니다.

## 현재 작업 위치

2026-10-09 사용자 요청에 따라 기존 `openwrt-wemo-matter-bridge`의 main으로
작업을 통합했습니다. 아래의 분리 checkout/branch 설명은 최초 검증 이력입니다.
앞으로 개발과 운영 배포 준비는 기존 디렉터리에서 계속합니다.
새 패키지와 롤백 후보, 체크섬, 빌드 증거는 이 checkout의 Git 제외 경로
`private/netcam-deployment-2026-10-09/`에 보존했습니다.
[운영 배포 준비 기록](NETCAM_SENSOR_DEPLOYMENT.md)을 함께 읽으세요.
운영 브리지에 패키지를 설치하거나 fabric을 변경한 것은 아닙니다.

## 완료한 구현

- 정확한 `urn:Belkin:device:NetCamSensor:1`만 새 Occupancy 센서로 분류합니다. 기존 플러그·조명·generic WeMo Sensor endpoint 동작을 유지합니다.
- `GetMotionStatus`와 GENA의 BinaryState/MotionState/DetectionAvailable을 하나의 packed snapshot으로 전달합니다. snapshot과 관측 시각을 원자적으로 저장하고 목록·이벤트가 같은 캐시를 읽습니다.
- 유효한 idle/motion 관측만 Matter Occupancy를 갱신합니다. 최초 관측 전 값0은 비어 있는 nonnullable 속성의 초기값이며 Reachable=false입니다. paused/unavailable/setup/unknown/legacy/offline에서는 마지막 유효 Occupancy를 보존하고 Reachable=false로 표시합니다.
- 구FW BinaryState는 metadata에만 저장하고 availability는 unknown으로 유지합니다. GetMotionStatus의 InvalidAction401만 legacy fallback을 허용합니다. 관련 없는 SOAP action이나 일시 오류는 legacy 판정 근거가 아닙니다. modern GENA에서 새 필드가 없으면 기존 관측과 freshness를 유지합니다.
- 기존 worker의 기본300초 조회와 600초 freshness 한계를 사용합니다. 움직임 없는 GENA 구독이 영구적으로 fresh 상태로 남지 않습니다.
- Occupancy device type0x0107/revision4, Occupancy cluster revision5/Vision feature, mandatory Identify(kNone)를 제공합니다. pinned SDK의 deprecated SensorType에는 Vision enum이 없어 호환값0/bitmap0을 사용하며 PIR 하드웨어를 주장하지 않습니다. 센서 NodeLabel 쓰기는 UnsupportedWrite입니다.
- 센서 endpoint는 별도 sensor_endpoints.db의 0x8000..0xfffd 범위에 지속 저장합니다. 센서 등록은 기존 endpoint allocator를 진행시키지 않습니다. 충돌·DB/COMMIT 실패는 등록을 거부하고 slot을 비우며 unpublished slot에 대한 이벤트를 무시합니다.
- IPCv3로 struct 크기와 protocol version을 함께 검사합니다. controller/engine/Matter app은 반드시 묶어서 업데이트합니다. mixed version과 잘못된 payload size를 거부합니다.

## 검증

분리 checkout `netcam-sensor-integration`, branch `codex/netcam-sensor`, 기준 commit `8f16ca0916`에서 작업했습니다. 기존 checkout과 운영 bridge/fabric은 변경하지 않았습니다.

```sh
python3 package/network/services/openwemo-bridge-core/src/tests/run_netcam_tests.py
git diff --check
```

5개 host suite가 통과했습니다. 실제 discovery/GENA/목록 생성 경로, flush 전 캐시 일관성, modern 이벤트 필드 누락, legacy fallback, malformed snapshot, IPC version/size, adapter 상태·inventory 실패, plug/light/generic 회귀, endpoint 재시작/순서 보존, SQLite COMMIT_BUSY rollback, publication/event 공통 guard를 검사합니다. 전체 CHIP main의 host 실행 대신 실제 target clean compile로 SDK 통합을 확인했습니다.

controller r22와 Matter app r23의 paired target clean build가 성공했습니다. 외부 make는 -j4였지만 app ninja는 실제 -j1로 실행됐습니다. controller C/H38개는 기존 OpenWrt patch를 적용한 소스와 같고, copied Matter C/C++/H16개도 worktree와 일치합니다. 원 SDK와 운영 서비스는 변경하지 않았습니다.

| APK | bytes | SHA256 |
| --- | ---: | --- |
| openwemo-bridge-core-0.1-r22.apk | 84101 | eeebb612bf8bbd502784f0824feda63d6cd141142123bcbd868f59118e387799 |
| wemo-matter-bridge-2025.12.01~8effa808-r23.apk | 535997 | 20e4c4daa0755b0c2a2a9cd4ffab5115257c1774e3cf11fc77e5b8dfa607206f |

아키텍처는 mipsel_24kc입니다. APK는 unsigned local 개발 산출물이며, SDK signing key는 운영 trusted key와 다릅니다. 기존 TROUBLESHOOTING의 허용된 local APK 절차는 두 파일을 정확히 지정하고 build/transfer SHA를 검증한 transaction에만 --allow-untrusted를 적용합니다. global trusted keys/repository 검증을 변경하지 않습니다.

## 남은 운영 검증과 용량 제약

실기 배포는 수행하지 않았습니다. read-only 확인 당시 controller r21/app r22이며 DB에 NetCam 후보는0개였습니다. 실제 commissioning된 관리자의 Descriptor PartsList·device type·unique identity를 private baseline으로 확보해야 합니다. 현재 startup binding 로그와 기존 local admin reader를 찾지 못해 기존 commissioned endpoint ID 보존의 실기 gate는 충족되지 않았습니다. host allocator 시험을 현재 commissioned mapping 확인으로 해석하면 안 됩니다.

16M 장치의 overlay 가용 공간은748KiB였습니다. 새 installed Matter 바이너리는1262291B, controller132024B/lib66031B/client66111B입니다. 정적 crypto를 링크하던 client2904043B는 이미 설치된 libcrypto.so.3 동적 링크로 줄였습니다. 기존 Matter는 upper이고 controller/lib/client는 lower입니다. 최종 두 패키지의 전체 논리 payload는1557283B, 4KiB별 zlib+inode 근사는748042B로 가용765952B와 차이는17910B뿐입니다. 실제 JFFS2 compression/GC, package DB, transaction 임시 공간을 보수적으로 보장하기 어렵습니다. 작은 APK가 설치 가능성의 증거는 아니며 현재 live 설치는 보류합니다. /data 우회, 파일 삭제, bridge image flash로 공간을 확보하는 작업은 별도 범위입니다.

배포 판단 전 실제 endpoint baseline과 설치 공간 대책을 검토해야 합니다. private evidence의 상세 계획은 watchdog→Matter app→controller stop, 두 APK 한 transaction 설치(--network=no/--scripts=no), controller→app→watchdog start, 기존 endpoint mapping 비교 및 기존 플러그·조명 회귀 확인을 요구합니다. rollback도 old r21/r22 APK를 함께 설치하며 fabric과 CHIP 파일을 삭제·reset·recommission하지 않습니다. 명시적 local APK로 r2→r1 downgrade하는 동작은 apk3.0.5의 disposable fixture DB에서 확인했습니다.

### Google Home 읽기 전용 baseline

root가 기존 로그인된 Galaxy S20+의 Google Home 화면에서 기존 플러그·조명과 bridge 설정의 등록 상태 및 표시 상태를 확인했습니다. 전원·이름·방·삭제·commissioning을 변경하지 않았습니다. 화면과 UI hierarchy 증거는 별도 private 보관이며 이 개발 문서에 개인 기기 식별자나 화면을 넣지 않습니다.

이 관찰은 기존 사용자 화면의 baseline입니다. 실제 Matter endpoint ID/UniqueID 읽기와 같지 않으며 endpoint ID 보존의 실기 증명이 아닙니다. 작은 overlay의 설치 공간 gate는 계속 미충족으로 유지합니다. 이후 승인된 배포가 있다면 동일 Google Home의 기존 플러그·조명 및 bridge 표시를 전후 비교하고 기존 기능과 NetCam 센서 수락을 확인해야 합니다.

실기 gate 이후에는 NetCam motion→idle, 재생 중 paused, unavailable/offline에서 마지막 Occupancy 유지와 Reachable 변경을 기존 commissioned 관리자로 확인해야 합니다. Matter camera/video 통합은 이 변경에 포함하지 않습니다.

### 동적 crypto packaging 검증

wemo_client의 정적 libcrypto.a 링크는 최초 client package 추가 때 도입됐으며 현재 문서/commit에 별도 ABI 이유가 없습니다. package가 이미 libopenssl3에 의존하므로 host client처럼 -lcrypto로 변경했습니다. 실제 packaged client는66111B입니다. 운영 libcrypto.so.3의 versioned crypto exports와 imports가 일치하고 필요한 shared library가 운영에 있습니다.

승인된 loader 검사에서 실제 packaged binary를 strict SSH로 /tmp에만 복사하고 host/target SHA256 일치를 확인했습니다. 실제 musl loader --list가 exit0으로 모든 dependency를 찾았습니다. client main이나 UPnP 명령은 실행하지 않았고 임시 파일은 제거했습니다. 원 운영 바이너리·설정·서비스·fabric은 유지했습니다. core clean 재빌드 전후 engine archive가 같아 Matter APK는 최종 core와 동일한 ABI에 연결돼 있습니다. 이는 loader/linker 검증이며 실제 센서 endpoint의 Google Home 수락 시험은 아닙니다.
