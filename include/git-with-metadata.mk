# Copyright (c) 2025 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

GWM_SH:=$(patsubst %.mk,%.sh,$(abspath $(lastword $(MAKEFILE_LIST))))

# Hook function that gathers git metadata; it can be overridden by the
# package Makefile. The output of this shell command (or commands) is
# captured into a .git-metadata file located at the root of the source tree.
define GitWithMetadata/gather
git show -s --format="COMMIT_TIMESTAMP=%ct" HEAD
endef

# Expands into a shell expression that resolves a variable from
# .git-metadata at build time, usually for passing as an argument
# to configure or cmake.
# Example: $(call GitWithMetadata/resolve,COMMIT_HASH)
define GitWithMetadata/resolve
"`. .git-metadata && echo "$$$$$(1)"`"
endef

ifndef DownloadMethod/default
$(error git-with-metadata.mk must be included after package.mk)
endif

# Set up hooks and use a flattened git download recipe so OpenWrt's flock
# wrapper does not split the metadata hook and the delegated downloader.
$(eval dl_tar_pack=true dl_tar_pack && $(value dl_tar_pack))
define DownloadMethod/git-with-metadata
	gwm_gather_$(if $(filter skip,$(SUBMODULES)),checkout,submodules)() { $(GitWithMetadata/gather) ; } && \
	SUBDIR=$(SUBDIR) && . $(GWM_SH) && \
	echo "Checking out files from the git repository..." && \
	mkdir -p $(TMP_DIR)/dl && \
	cd $(TMP_DIR)/dl && \
	rm -rf $(SUBDIR) && \
	[ \! -d $(SUBDIR) ] && \
	git clone $(OPTS) $(URL) $(SUBDIR) && \
	(cd $(SUBDIR) && git checkout $(SOURCE_VERSION)) && \
	export TAR_TIMESTAMP=`cd $(SUBDIR) && git log -1 --no-show-signature --format=@%ct` && \
	echo "Generating formal git archive (apply .gitattributes rules)" && \
	(cd $(SUBDIR) && git config core.abbrev 8 && \
	git archive --format=tar HEAD --output=../$(SUBDIR).tar.git) && \
	$(if $(filter skip,$(SUBMODULES)),true, \
		$(TAR) --numeric-owner --owner=0 --group=0 --ignore-failed-read -C $(SUBDIR) -f $(SUBDIR).tar.git -r .git .gitmodules 2>/dev/null \
	) && \
	rm -rf $(SUBDIR) && mkdir $(SUBDIR) && \
	$(TAR) -C $(SUBDIR) -xf $(SUBDIR).tar.git && \
	(cd $(SUBDIR) && $(if $(filter skip,$(SUBMODULES)),true,git submodule update --init --recursive -- $(SUBMODULES) && \
	rm -rf .git .gitmodules)) && \
	echo "Packing checkout..." && \
	true dl_tar_pack && \
	$(TAR) --numeric-owner --owner=0 --group=0 --mode=a-s --sort=name \
		$$$${TAR_TIMESTAMP:+--mtime="$$$$TAR_TIMESTAMP"} -c $(SUBDIR) | $(call dl_pack,$(TMP_DIR)/dl/$(FILE)) && \
	mv $(TMP_DIR)/dl/$(FILE) $(DL_DIR)/ && \
	rm -rf $(SUBDIR);
endef
