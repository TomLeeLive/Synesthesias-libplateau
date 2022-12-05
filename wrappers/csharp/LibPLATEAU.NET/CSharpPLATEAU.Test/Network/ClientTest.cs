﻿using Microsoft.VisualStudio.TestTools.UnitTesting;
using PLATEAU.Network;

namespace PLATEAU.Test.Network
{
    [TestClass]
    public class ClientTest
    {
        [TestMethod]
        public void DatasetMetadataGroup()
        {
            using var client = Client.Create();
            client.Url = NetworkConfig.MockServerURL;
            var metadataGroup = client.GetDatasetMetadataGroup();
            Assert.AreEqual("東京都", metadataGroup.At(0).Title);
            Assert.AreEqual("23区", metadataGroup.At(0).Datasets.At(0).Title);
        }

        [TestMethod]
        public void GetAndSetUrl()
        {
            using var client = Client.Create();
            client.Url = "https://dummy.com";
            Assert.AreEqual("https://dummy.com", client.Url);
        }
    }
}